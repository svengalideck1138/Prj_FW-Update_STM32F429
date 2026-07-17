using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Threading;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UI_Monitor
{
    public partial class Form1 : Form
    {
        // RS-232 통신용 시리얼 포트
        private readonly SerialPort _serial = new SerialPort();

        // 앱 실행영역 시작 주소 (flash_if.h의 APP_ADDRESS와 일치)
        private const uint APP_BASE_ADDRESS = 0x08020000;

        // [임시 테스트] true면 처음 몇 청크의 '첫 전송'에 일부러 틀린 CRC16을 보내 재전송을 유발.
        // 청크별 checksum + 자동 재전송이 동작하는지 확인용. (검증 완료 → 평소엔 false)
        private bool _debugCorruptFirstAttempt = false;

        // Open으로 로드된 펌웨어 이미지 (Download 시 전송)
        private byte[] _fwImage;

        // 전송 진행률/속도(실시간 표출) 계산용
        private int _txStartMs;
        private int _lastLoggedDecile;

        public Form1()
        {
            InitializeComponent();

            // 시작 시 기본 모드(RS-232)에 맞춰 화면 상태를 한 번 맞춰준다.
            ApplyTransferMode();

            // --- 시리얼 관련 이벤트 연결 (Designer를 건드리지 않고 여기서 배선) ---
            Btn_COMRefresh.Click += (s, e) => RefreshComPorts();
            Btn_COMOPEN.Click += Btn_COMOPEN_Click;
            Btn_COMCLOSE.Click += Btn_COMCLOSE_Click;
            Btn_Clear_RS232Log.Click += (s, e) => Tbox_RS232_ReceivedLog.Clear();
            _serial.DataReceived += Serial_DataReceived;
            FormClosing += (s, e) => { if (_serial.IsOpen) _serial.Close(); };

            // 펌웨어 업데이트 버튼 배선 (Designer 컨트롤)
            Btn_FW_Open.Click += Btn_FW_Open_Click;
            Btn_FW_Download.Click += Btn_FW_Download_Click;

            // COM 포트 목록 채우고 초기 버튼 상태 설정
            RefreshComPorts();
            UpdateConnState(false);
        }

        /// <summary>
        /// Transfer Mode 라디오 버튼(RS-232 / Ethernet)이 바뀔 때 호출된다.
        /// </summary>
        private void TransMode_CheckedChanged(object sender, EventArgs e)
        {
            ApplyTransferMode();
        }

        /// <summary>
        /// 현재 선택된 Transfer Mode에 따라 Received Viewer와 설정 패널을 전환한다.
        /// RS-232: RS-232 뷰어 표시 / Com Port 설정 활성화
        /// Ethernet: Ethernet 뷰어 표시 / Socket 설정 활성화
        /// </summary>
        private void ApplyTransferMode()
        {
            bool isRS232 = RBtn_TransMode_RS232.Checked;

            // 수신 뷰어 전환 (같은 위치에 겹쳐 있으므로 Visible로 하나만 노출)
            GroupBox_ViewerRS232.Visible = isRS232;
            GroupBox_ViewerEthernet.Visible = !isRS232;

            // 연결 설정 패널도 모드에 맞춰 활성/비활성
            GroupBox_ComportSetting.Enabled = isRS232;
            GroupBox_SocketSetting.Enabled = !isRS232;
        }

        // ============================================================
        //  시리얼 통신
        // ============================================================

        /// <summary>사용 가능한 COM 포트 목록을 콤보박스에 채운다.</summary>
        private void RefreshComPorts()
        {
            string prev = CBoxCOMPORT.SelectedItem?.ToString();
            CBoxCOMPORT.Items.Clear();
            CBoxCOMPORT.Items.AddRange(SerialPort.GetPortNames());

            if (prev != null && CBoxCOMPORT.Items.Contains(prev))
                CBoxCOMPORT.SelectedItem = prev;
            else if (CBoxCOMPORT.Items.Count > 0)
                CBoxCOMPORT.SelectedIndex = 0;
        }

        /// <summary>OPEN 버튼: 선택된 설정으로 포트를 연다.</summary>
        private void Btn_COMOPEN_Click(object sender, EventArgs e)
        {
            if (_serial.IsOpen) return;

            if (CBoxCOMPORT.SelectedItem == null)
            {
                MessageBox.Show("COM 포트를 선택하세요.");
                return;
            }

            try
            {
                _serial.PortName = CBoxCOMPORT.SelectedItem.ToString();
                _serial.BaudRate = int.Parse(CBoxBaudRate.Text);
                _serial.DataBits = int.Parse(CBoxDataBits.Text);
                _serial.StopBits = ParseStopBits(CBoxStopBits.Text);
                _serial.Parity = ParseParity(CBoxParityBits.Text);
                _serial.Encoding = Encoding.ASCII;
                _serial.Open();

                UpdateConnState(true);
                AppendLog($"[OPEN] {_serial.PortName} @ {_serial.BaudRate} 8N1\r\n");
            }
            catch (Exception ex)
            {
                MessageBox.Show("포트 열기 실패: " + ex.Message);
            }
        }

        /// <summary>CLOSE 버튼: 포트를 닫는다.</summary>
        private void Btn_COMCLOSE_Click(object sender, EventArgs e)
        {
            if (_serial.IsOpen) _serial.Close();
            UpdateConnState(false);
            AppendLog("[CLOSE]\r\n");
        }

        /// <summary>데이터 수신(백그라운드 스레드) → 뷰어에 표시.</summary>
        private void Serial_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                string data = _serial.ReadExisting();
                AppendLog(data);
            }
            catch { /* 포트가 닫히는 중이면 무시 */ }
        }

        /// <summary>수신 로그 텍스트박스에 안전하게(UI 스레드) 문자열을 덧붙인다.</summary>
        private void AppendLog(string text)
        {
            if (Tbox_RS232_ReceivedLog.IsDisposed) return;

            if (Tbox_RS232_ReceivedLog.InvokeRequired)
                Tbox_RS232_ReceivedLog.BeginInvoke(new Action(() => AppendLog(text)));
            else
                Tbox_RS232_ReceivedLog.AppendText(text);
        }

        /// <summary>연결 상태에 따라 OPEN/CLOSE 버튼 활성화를 갱신한다.</summary>
        private void UpdateConnState(bool open)
        {
            Btn_COMOPEN.Enabled = !open;
            Btn_COMCLOSE.Enabled = open;
        }

        // ============================================================
        //  펌웨어 전송 (.hex/.bin -> Staging)
        // ============================================================

        /// <summary>Open 버튼: 파일 선택 → 파싱/로드 → 경로·크기 표시.</summary>
        private void Btn_FW_Open_Click(object sender, EventArgs e)
        {
            using (var dlg = new OpenFileDialog
            {
                Filter = "펌웨어 (*.hex;*.bin)|*.hex;*.bin|Intel HEX (*.hex)|*.hex|Binary (*.bin)|*.bin|모든 파일 (*.*)|*.*"
            })
            {
                if (dlg.ShowDialog() != DialogResult.OK) return;

                try
                {
                    _fwImage = dlg.FileName.EndsWith(".hex", StringComparison.OrdinalIgnoreCase)
                        ? ParseIntelHex(dlg.FileName, APP_BASE_ADDRESS)
                        : PadTo4(File.ReadAllBytes(dlg.FileName));

                    textBox_FW_FIle.Text = dlg.FileName;                       // 선택된 파일 경로 표시
                    label_FW_Size.Text = $"{_fwImage.Length:N0} Byte";  // 파싱된 이미지 크기 표시
                    AppendLog($"[FW] {Path.GetFileName(dlg.FileName)} 로드: {_fwImage.Length} bytes\r\n");
                }
                catch (Exception ex)
                {
                    _fwImage = null;
                    textBox_FW_FIle.Text = "";
                    label_FW_Size.Text = "0 Byte";
                    MessageBox.Show("펌웨어 파싱 실패: " + ex.Message);
                }
            }
        }

        /// <summary>F/W Download 버튼: 로드된 펌웨어를 프로토콜로 전송.</summary>
        private async void Btn_FW_Download_Click(object sender, EventArgs e)
        {
            if (!_serial.IsOpen)
            {
                MessageBox.Show("먼저 COM 포트를 여세요 (OPEN).");
                return;
            }
            if (_fwImage == null)
            {
                MessageBox.Show("먼저 Open으로 펌웨어 파일을 선택하세요.");
                return;
            }

            SetTransferUi(true);
            bool ok = await Task.Run(() => SendFirmwareSequence(_fwImage));
            SetTransferUi(false);
            MessageBox.Show(ok ? "펌웨어 전송 완료! (Staging에 저장됨)" : "펌웨어 전송 실패 — 로그를 확인하세요.");
        }

        /// <summary>프로토콜에 따라 펌웨어를 전송한다 (백그라운드 스레드).</summary>
        private bool SendFirmwareSequence(byte[] image)
        {
            const byte ACK = 0x79, NACK = 0x1F;
            const int CHUNK = 256;

            _serial.DataReceived -= Serial_DataReceived;   // 동기 읽기를 위해 이벤트 분리
            try
            {
                _serial.ReadTimeout = 10000;               // erase가 오래 걸릴 수 있어 넉넉히
                _serial.DiscardInBuffer();

                // 1) FWUPDATE 전송
                _serial.Write("FWUPDATE");
                AppendLog("[TX] FWUPDATE\r\n");

                // 2) READY 대기
                if (!WaitForText("READY", 3000))
                {
                    AppendLog("[ERR] READY 응답 없음 (보드가 초록 하트비트 상태인지 확인)\r\n");
                    return false;
                }
                Thread.Sleep(30);
                _serial.DiscardInBuffer();                 // "READY\r\n"의 \r\n 제거
                AppendLog("[RX] READY\r\n");

                // 3) 헤더 전송: [전체크기 4B][CRC32 4B] (little-endian)
                uint crc = Crc32Stm32(image);
                var header = new byte[8];
                BitConverter.GetBytes((uint)image.Length).CopyTo(header, 0);
                BitConverter.GetBytes(crc).CopyTo(header, 4);
                _serial.Write(header, 0, 8);
                AppendLog($"[TX] size={image.Length}, CRC32=0x{crc:X8}\r\n");

                // 4) Staging erase 후 ACK
                int b = _serial.ReadByte();
                if (b != ACK) { AppendLog($"[ERR] erase ACK 실패 (0x{b:X2})\r\n"); return false; }
                AppendLog($"[RX] ACK — 전송 시작 ({image.Length} bytes)\r\n");

                _txStartMs = Environment.TickCount;   // 실시간 속도 계산 기준 시각
                _lastLoggedDecile = -1;

                // 5) 청크 전송 — 각 청크: [데이터][CRC16 2B]. NACK면 같은 청크 재전송.
                int sent = 0;
                int totalRetries = 0;
                while (sent < image.Length)
                {
                    int n = Math.Min(CHUNK, image.Length - sent);
                    ushort chunkCrc = Crc16Ccitt(image, sent, n);
                    var crcBytes = new byte[] { (byte)(chunkCrc & 0xFF), (byte)(chunkCrc >> 8) };

                    int retries = 0;
                    while (true)
                    {
                        _serial.Write(image, sent, n);      // 데이터

                        // [임시 테스트] 처음 3개 청크(offset 0/256/512)의 첫 시도에만 CRC를 깨서 재전송 유발
                        byte[] txCrc = crcBytes;
                        if (_debugCorruptFirstAttempt && retries == 0 &&
                            (sent == 0 || sent == 256 || sent == 512))
                        {
                            txCrc = new byte[] { (byte)(crcBytes[0] ^ 0xFF), crcBytes[1] };
                        }
                        _serial.Write(txCrc, 0, 2);         // CRC16 (little-endian)

                        int resp = _serial.ReadByte();
                        if (resp == ACK) break;             // 확정 → 다음 청크
                        if (resp == NACK)
                        {
                            if (++retries > 5)
                            {
                                AppendLog($"[ERR] 청크 재시도 초과 @ offset {sent}\r\n");
                                return false;
                            }
                            totalRetries++;
                            AppendLog($"[!] 청크 CRC 불일치 → 재전송 @ {sent} (retry {retries})\r\n");
                            continue;                       // 같은 청크 재전송
                        }
                        AppendLog($"[ERR] 예상치 못한 응답 0x{resp:X2} @ offset {sent}\r\n");
                        return false;
                    }

                    sent += n;
                    UpdateProgress(sent, image.Length);
                }
                if (totalRetries > 0) AppendLog($"[i] 청크 재전송 총 {totalRetries}회 (자동 복구됨)\r\n");

                // 6) MCU가 Staging CRC를 검증한 결과 대기 (DONE 또는 CRCERR)
                string res = WaitForAnyText(new[] { "DONE", "CRCERR" }, 5000);
                if (res == "CRCERR")
                {
                    AppendLog("[ERR] CRC 불일치 — 전송이 손상됨 (적용되지 않음)\r\n");
                    return false;
                }
                if (res != "DONE") { AppendLog("[ERR] 완료 응답 없음\r\n"); return false; }

                AppendLog("[RX] DONE — CRC 검증 통과, 보드가 재부팅되어 적용됩니다\r\n");
                return true;
            }
            catch (TimeoutException) { AppendLog("[ERR] 타임아웃\r\n"); return false; }
            catch (Exception ex) { AppendLog("[ERR] " + ex.Message + "\r\n"); return false; }
            finally
            {
                _serial.DataReceived += Serial_DataReceived;   // 이벤트 복원
            }
        }

        /// <summary>target 문자열이 나올 때까지 동기 수신 (timeout 초과 시 false).</summary>
        private bool WaitForText(string target, int timeoutMs)
        {
            int old = _serial.ReadTimeout;
            _serial.ReadTimeout = timeoutMs;
            var sb = new StringBuilder();
            try
            {
                while (true)
                {
                    sb.Append((char)_serial.ReadByte());
                    if (sb.ToString().Contains(target)) return true;
                }
            }
            catch (TimeoutException) { return false; }
            finally { _serial.ReadTimeout = old; }
        }

        /// <summary>여러 target 중 하나가 나올 때까지 동기 수신. 매칭된 문자열 반환(타임아웃 시 null).</summary>
        private string WaitForAnyText(string[] targets, int timeoutMs)
        {
            int old = _serial.ReadTimeout;
            _serial.ReadTimeout = timeoutMs;
            var sb = new StringBuilder();
            try
            {
                while (true)
                {
                    sb.Append((char)_serial.ReadByte());
                    string s = sb.ToString();
                    foreach (string t in targets)
                        if (s.Contains(t)) return t;
                }
            }
            catch (TimeoutException) { return null; }
            finally { _serial.ReadTimeout = old; }
        }

        /// <summary>
        /// STM32 하드웨어 CRC 유닛과 동일한 CRC32를 계산한다.
        /// poly=0x04C11DB7, init=0xFFFFFFFF, 32bit word 단위, 반사 없음, 최종 XOR 없음.
        /// </summary>
        private static uint Crc32Stm32(byte[] data)
        {
            uint crc = 0xFFFFFFFF;
            for (int i = 0; i + 4 <= data.Length; i += 4)
            {
                crc ^= BitConverter.ToUInt32(data, i);   // little-endian으로 word 구성
                for (int bit = 0; bit < 32; bit++)
                    crc = (crc & 0x80000000) != 0 ? (crc << 1) ^ 0x04C11DB7 : (crc << 1);
            }
            return crc;
        }

        /// <summary>CRC16-CCITT (poly=0x1021, init=0xFFFF, 반사 없음) — 청크 무결성 검사용.</summary>
        private static ushort Crc16Ccitt(byte[] data, int offset, int len)
        {
            ushort crc = 0xFFFF;
            for (int i = 0; i < len; i++)
            {
                crc ^= (ushort)(data[offset + i] << 8);
                for (int b = 0; b < 8; b++)
                    crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021) : (ushort)(crc << 1);
            }
            return crc;
        }

        /// <summary>진행률을 상태바에 표시 (UI 스레드).</summary>
        private void UpdateProgress(int sent, int total)
        {
            if (StatusStrip1.InvokeRequired)
            {
                StatusStrip1.BeginInvoke(new Action(() => UpdateProgress(sent, total)));
                return;
            }
            int pct = total > 0 ? (int)((long)sent * 100 / total) : 0;

            // 경과 시간 → 전송 속도(KB/s) 계산
            int elapsedMs = Math.Max(1, Environment.TickCount - _txStartMs);
            double kbps = (sent / 1024.0) / (elapsedMs / 1000.0);

            // 상태바: 진행률 바 + % + 바이트/속도 (실시간)
            TStripProBar_SendState.Maximum = 100;
            TStripProBar_SendState.Value = pct;
            TSStatusLb_Percent.Text = $"{pct}%";
            TSStatusLb_nCounter.Text = $"{sent:N0}/{total:N0} B   {kbps:F1} KB/s";

            // 로그: 10%마다 한 줄(흐름을 뷰어에서도 볼 수 있게, 과도한 스팸 방지)
            int decile = pct / 10;
            if (decile != _lastLoggedDecile)
            {
                _lastLoggedDecile = decile;
                AppendLog($"[전송] {pct,3}%  {sent:N0}/{total:N0} bytes  {kbps:F1} KB/s\r\n");
            }
        }

        /// <summary>전송 중 UI 잠금/해제.</summary>
        private void SetTransferUi(bool active)
        {
            if (InvokeRequired) { BeginInvoke(new Action(() => SetTransferUi(active))); return; }
            Btn_FW_Open.Enabled = !active;
            Btn_FW_Download.Enabled = !active;
            Btn_COMOPEN.Enabled = !active && !_serial.IsOpen;
            Btn_COMCLOSE.Enabled = !active && _serial.IsOpen;
            if (!active) TStripProBar_SendState.Value = 0;
        }

        /// <summary>Intel HEX 파일을 파싱해 baseAddress 기준 연속 바이너리로 변환한다.</summary>
        private static byte[] ParseIntelHex(string path, uint baseAddress)
        {
            var records = new List<KeyValuePair<uint, byte[]>>();
            uint upper = 0;
            uint maxEnd = baseAddress;
            bool any = false;

            foreach (string raw in File.ReadAllLines(path))
            {
                string line = raw.Trim();
                if (line.Length < 11 || line[0] != ':') continue;

                int len = Convert.ToInt32(line.Substring(1, 2), 16);
                int addr16 = Convert.ToInt32(line.Substring(3, 4), 16);
                int type = Convert.ToInt32(line.Substring(7, 2), 16);
                string dataHex = line.Substring(9, len * 2);

                if (type == 0x00)          // 데이터 레코드
                {
                    uint abs = (upper << 16) + (uint)addr16;
                    var data = new byte[len];
                    for (int i = 0; i < len; i++)
                        data[i] = Convert.ToByte(dataHex.Substring(i * 2, 2), 16);
                    records.Add(new KeyValuePair<uint, byte[]>(abs, data));
                    if (abs + (uint)len > maxEnd) maxEnd = abs + (uint)len;
                    any = true;
                }
                else if (type == 0x04)     // 확장 선형 주소 (상위 16비트)
                {
                    upper = (uint)Convert.ToInt32(dataHex, 16);
                }
                else if (type == 0x01)     // EOF
                {
                    break;
                }
            }

            if (!any) throw new Exception("데이터 레코드가 없습니다.");

            uint size = (maxEnd - baseAddress + 3u) & ~3u;   // 4바이트 정렬
            var image = new byte[size];
            for (int i = 0; i < image.Length; i++) image[i] = 0xFF;   // 빈 공간 0xFF

            foreach (var rec in records)
            {
                if (rec.Key < baseAddress)
                    throw new Exception($"주소 0x{rec.Key:X8}가 앱 시작(0x{baseAddress:X8})보다 낮습니다.");
                Array.Copy(rec.Value, 0, image, (int)(rec.Key - baseAddress), rec.Value.Length);
            }
            return image;
        }

        /// <summary>길이를 4바이트 배수로 맞춰(0xFF 패딩) 반환한다.</summary>
        private static byte[] PadTo4(byte[] data)
        {
            int size = (data.Length + 3) & ~3;
            if (size == data.Length) return data;
            var padded = new byte[size];
            for (int i = 0; i < size; i++) padded[i] = 0xFF;
            Array.Copy(data, padded, data.Length);
            return padded;
        }

        private static StopBits ParseStopBits(string s)
        {
            switch (s)
            {
                case "2": return StopBits.Two;
                case "1.5": return StopBits.OnePointFive;
                default: return StopBits.One;
            }
        }

        private static Parity ParseParity(string s)
        {
            switch (s)
            {
                case "Even": return Parity.Even;
                case "Odd": return Parity.Odd;
                default: return Parity.None;
            }
        }
    }
}
