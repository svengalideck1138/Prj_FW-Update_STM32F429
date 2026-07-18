using System;
using System.IO;
using System.IO.Ports;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace UI_Monitor
{
    /// <summary>
    /// 펌웨어 전송 링크 추상화. 프로토콜(SendFirmwareSequence)이 UART/TCP에 무관하게
    /// 동작하도록 바이트 I/O만 노출한다. SerialPort / TcpClient 위에 각각 구현한다.
    /// </summary>
    internal interface IFwLink : IDisposable
    {
        bool IsConnected { get; }
        int ReadTimeout { get; set; }
        void DiscardInBuffer();
        void Write(string ascii);                     // ASCII로 인코딩해 송신
        void Write(byte[] buffer, int offset, int count);
        int ReadByte();                               // ReadTimeout 초과 시 TimeoutException
        void PauseAsyncRx();                          // 동기 프로토콜 동안 비동기 수신 중지
        void ResumeAsyncRx();
    }

    /// <summary>기존 RS-232(SerialPort) 백엔드. 뷰어용 비동기 수신 핸들러를 잠시 떼었다 붙인다.</summary>
    internal sealed class SerialLink : IFwLink
    {
        private readonly SerialPort _port;
        private readonly SerialDataReceivedEventHandler _asyncHandler;

        public SerialLink(SerialPort port, SerialDataReceivedEventHandler asyncHandler)
        {
            _port = port;
            _asyncHandler = asyncHandler;
        }

        public bool IsConnected => _port.IsOpen;
        public int ReadTimeout { get => _port.ReadTimeout; set => _port.ReadTimeout = value; }
        public void DiscardInBuffer() => _port.DiscardInBuffer();
        public void Write(string ascii)
        {
            byte[] b = Encoding.ASCII.GetBytes(ascii);
            _port.Write(b, 0, b.Length);
        }
        public void Write(byte[] buffer, int offset, int count) => _port.Write(buffer, offset, count);
        public int ReadByte() => _port.ReadByte();    // SerialPort는 타임아웃 시 TimeoutException을 던진다

        public void PauseAsyncRx()  { if (_asyncHandler != null) _port.DataReceived -= _asyncHandler; }
        public void ResumeAsyncRx() { if (_asyncHandler != null) _port.DataReceived += _asyncHandler; }

        public void Dispose() { /* 포트 수명은 Form이 관리(_serial) → 여기서 닫지 않음 */ }
    }

    /// <summary>Ethernet(TCP) 백엔드. 보드(STM32)가 서버, 이 앱이 클라이언트로 접속한다.</summary>
    internal sealed class TcpLink : IFwLink
    {
        private readonly TcpClient _client;
        private readonly NetworkStream _stream;

        /// <summary>ip:port로 접속(동기, connectTimeoutMs 초과 시 예외).</summary>
        public TcpLink(string ip, int port, int connectTimeoutMs = 3000)
        {
            _client = new TcpClient();
            IAsyncResult ar = _client.BeginConnect(IPAddress.Parse(ip), port, null, null);
            if (!ar.AsyncWaitHandle.WaitOne(connectTimeoutMs))
            {
                _client.Close();
                throw new TimeoutException($"{ip}:{port} 연결 시간 초과");
            }
            _client.EndConnect(ar);          // 실패 시 예외
            _client.NoDelay = true;          // Nagle 비활성(1B ACK 저지연)
            _stream = _client.GetStream();
        }

        public bool IsConnected => _client != null && _client.Connected;

        public int ReadTimeout
        {
            get => _stream.ReadTimeout;
            set => _stream.ReadTimeout = (value <= 0) ? Timeout.Infinite : value;
        }

        public void DiscardInBuffer()
        {
            try
            {
                var tmp = new byte[512];
                while (_stream.DataAvailable) _stream.Read(tmp, 0, tmp.Length);
            }
            catch { /* 무시 */ }
        }

        public void Write(string ascii)
        {
            byte[] b = Encoding.ASCII.GetBytes(ascii);
            _stream.Write(b, 0, b.Length);
        }
        public void Write(byte[] buffer, int offset, int count) => _stream.Write(buffer, offset, count);

        public int ReadByte()
        {
            try
            {
                int v = _stream.ReadByte();
                if (v < 0) throw new IOException("연결이 닫힘(원격 종료)");
                return v;
            }
            catch (IOException ex) when (ex.InnerException is SocketException se
                                         && se.SocketErrorCode == SocketError.TimedOut)
            {
                /* NetworkStream은 타임아웃을 IOException(→SocketException TimedOut)으로 던진다.
                 * 프로토콜의 WaitForText 등이 TimeoutException을 잡도록 변환한다. */
                throw new TimeoutException("read timeout", ex);
            }
        }

        /* 보드는 요청받았을 때만 응답한다(자발적 송신 없음) → 상시 리더가 필요 없다.
         * 상태 표시는 Form1의 타이머가 FWINFO??를 폴링해 가져간다. */
        public void PauseAsyncRx()  { /* 비동기 수신 리더 없음 → no-op */ }
        public void ResumeAsyncRx() { /* no-op */ }

        public void Dispose()
        {
            try { _stream?.Dispose(); } catch { }
            try { _client?.Close(); } catch { }
        }
    }
}
