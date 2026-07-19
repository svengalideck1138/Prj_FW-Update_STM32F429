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

        // 전송 링크 추상화: 시리얼 래퍼(항상 존재) + TCP 링크(연결 시에만)
        private SerialLink _serialLink;
        private TcpLink _tcp;

        // Ethernet 상태 폴링 타이머(1초). Tick은 UI 스레드에서 돈다 — 전송(Task.Run)과
        // 끼어들지 않도록 SetTransferUi가 시작/정지를 관리한다.
        private readonly System.Windows.Forms.Timer _infoTimer = new System.Windows.Forms.Timer();

        // 앱 실행영역 시작 주소 (flash_if.h의 APP_ADDRESS와 일치)
        private const uint APP_BASE_ADDRESS = 0x08020000;

        // 슬롯 크기 (flash_if.h의 APP_SIZE / STAGING_SIZE / FACTORY_SIZE — 셋 다 512KB로 동일).
        // 이미지가 이보다 크면 MCU가 헤더 단계에서 NACK하고 Wdg_Panic으로 리셋해 버리므로,
        // 보드를 건드리기 전에 PC에서 먼저 거른다.
        private const uint SLOT_SIZE = 512u * 1024u;

        // 이미지 선두 4바이트(초기 스택 포인터)의 유효 범위.
        // 부트로더 BL_JumpToApplication()과 앱 Ota_RequestRollback()이 쓰는 것과 같은 기준 —
        // 같은 판정을 PC에서 미리 해두면 헛된 전송과 그에 따른 롤백을 막을 수 있다.
        private const uint RAM_START = 0x20000000;
        private const uint RAM_END = 0x20030000;   // 0x20000000 + 192KB

        // [임시 테스트] true면 처음 몇 청크의 '첫 전송'에 일부러 틀린 CRC16을 보내 재전송을 유발.
        // 청크별 checksum + 자동 재전송이 동작하는지 확인용. (검증 완료 → 평소엔 false)
        private bool _debugCorruptFirstAttempt = false;

        // Open으로 로드된 펌웨어 이미지 (Download 시 전송).
        // APP(Staging)용과 FACTORY용을 각각 따로 들고 있어야 서로 다른 펌웨어를 넣어
        // '롤백이 실제로 복원되는지'를 눈으로 확인할 수 있다.
        private byte[] _fwImage;        // FW_APP  → Btn_FW_Open      / Btn_FW_Download
        private byte[] _factoryImage;   // FACTORY → Btn_FACTORY_Open / Btn_FACTORY_Download

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
            _serialLink = new SerialLink(_serial, Serial_DataReceived);   // 프로토콜용 시리얼 래퍼
            FormClosing += (s, e) =>
            {
                _infoTimer.Stop();
                if (_serial.IsOpen) _serial.Close();
                _tcp?.Dispose();
            };

            // 펌웨어 업데이트 버튼 배선 (Designer 컨트롤)
            Btn_FW_Open.Click += Btn_FW_Open_Click;
            Btn_FW_Download.Click += Btn_FW_Download_Click;
            Btn_FACTORY_Open.Click += Btn_FACTORY_Open_Click;
            Btn_FACTORY_Download.Click += Btn_FACTORY_Download_Click;

            // 슬롯 전환(전송 없이 재부팅만으로 App 내용을 바꾼다)
            Btn_ToFACTORY.Click += Btn_ToFACTORY_Click;

            // 대상 슬롯 주소를 참고용으로 표시 (Designer에서 Enabled=false, 읽기 전용).
            // 실제 기록 위치는 펌웨어(flash_if.h)가 정하며 PC는 주소를 보내지 않는다 —
            // 임의 주소를 못 보내므로 부트로더/메타데이터를 덮어쓸 위험이 원천 차단된다.
            textBox_APP_Address.Text = "0x08020000 (512KB)";
            textBox_FACT_Address.Text = "0x080A0000 (512KB)";

            // --- 소켓(Ethernet) 관련 이벤트 연결 ---
            Btn_SOCKETConnect.Click += Btn_SOCKETConnect_Click;
            Btn_SOCKETDisconnect.Click += Btn_SOCKETDisconnect_Click;
            Btn_Clear_EthernetLog.Click += (s, e) => Tbox_Ethernet_ReceivedLog.Clear();

            _infoTimer.Interval = 1000;                       // RS-232 배너와 같은 주기
            _infoTimer.Tick += (s, e) => PollFirmwareInfo();

            // COM 포트 목록 채우고 초기 버튼 상태 설정
            RefreshComPorts();
            UpdateConnState(false);
            UpdateSocketConnState(false);
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

            // 쓰지 않게 된 쪽 연결은 자동으로 닫는다.
            // 사용자가 Close/Disconnect를 잊고 모드만 바꾸면 그 연결이 배경에 남아
            // 포트를 계속 점유하고(다른 프로그램이 COM을 못 엶), Ethernet 쪽은 폴링
            // 타이머가 계속 돌며 보이지도 않는 뷰어에 로그를 쌓는다.
            if (isRS232)
            {
                if (_tcp != null)
                {
                    _infoTimer.Stop();
                    _tcp.Dispose();
                    _tcp = null;
                    UpdateSocketConnState(false);
                    Log(LogCh.Eth, LogKind.Info, "DISCONNECT — RS-232 모드로 전환하여 소켓을 닫았습니다.");
                }
            }
            else
            {
                if (_serial.IsOpen)
                {
                    _serial.Close();
                    UpdateConnState(false);
                    Log(LogCh.Serial, LogKind.Info, "CLOSE — Ethernet 모드로 전환하여 COM 포트를 닫았습니다.");
                }
            }

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
                Log(LogCh.Serial, LogKind.Info, $"OPEN {_serial.PortName} @ {_serial.BaudRate} 8N1");
                QuerySysInfo(_serialLink, LogCh.Serial);  // 보드/메모리 정보 1회
                Log(LogCh.Serial, LogKind.State, "State - Normal");
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
            FlushSerialRx();                       // 줄바꿈 못 받은 잔여분 출력
            UpdateConnState(false);
            Log(LogCh.Serial, LogKind.Info, "CLOSE");
        }

        /// <summary>데이터 수신(백그라운드 스레드) → 뷰어에 표시.</summary>
        private void Serial_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                string data = _serial.ReadExisting();
                FeedSerialRx(data);
            }
            catch { /* 포트가 닫히는 중이면 무시 */ }
        }

        /// <summary>
        /// 시리얼 수신 조각을 줄 단위로 모아 태그를 붙인다.
        /// ⚠️ ReadExisting()은 '줄'이 아니라 임의 크기 덩어리를 준다. 조각마다 바로
        ///    타임스탬프를 붙이면 한 줄이 두 개로 쪼개지거나 여러 줄이 한 태그에 뭉친다.
        ///    그래서 \n 이 올 때까지 버퍼에 모았다가 완성된 줄만 내보낸다.
        /// </summary>
        private void FeedSerialRx(string chunk)
        {
            if (string.IsNullOrEmpty(chunk)) return;

            lock (_serialRxLock)
            {
                _serialRxBuf.Append(chunk);

                for (;;)
                {
                    string s = _serialRxBuf.ToString();
                    int nl = s.IndexOf('\n');
                    if (nl < 0) break;

                    string line = s.Substring(0, nl).TrimEnd('\r');
                    _serialRxBuf.Remove(0, nl + 1);

                    if (line.Length > 0) Log(LogCh.Serial, LogKind.Rx, line);
                }

                // 보드가 줄바꿈 없이 계속 뱉는 비정상 상황에서 버퍼가 무한히 자라지 않도록.
                if (_serialRxBuf.Length > 4096)
                {
                    Log(LogCh.Serial, LogKind.Rx, _serialRxBuf.ToString());
                    _serialRxBuf.Clear();
                }
            }
        }

        /// <summary>포트를 닫을 때 줄바꿈을 못 받은 잔여분을 흘려보낸다.</summary>
        private void FlushSerialRx()
        {
            lock (_serialRxLock)
            {
                if (_serialRxBuf.Length > 0)
                {
                    Log(LogCh.Serial, LogKind.Rx, _serialRxBuf.ToString().TrimEnd('\r'));
                    _serialRxBuf.Clear();
                }
            }
        }

        /// <summary>로그를 어느 뷰어에 찍을지. 화면 선택이 아니라 '출처'로 정한다.</summary>
        private enum LogCh
        {
            Serial,   // RS-232에서 받았거나 COM 포트에 대한 메시지 → 항상 RS-232 뷰어
            Eth,      // TCP에서 받았거나 소켓에 대한 메시지        → 항상 Ethernet 뷰어
            Auto      // 출처가 정해지지 않은 메시지 → 진행 중인 작업, 없으면 현재 모드
        }

        /// <summary>
        /// 전송/슬롯전환이 진행 중일 때 그 작업이 쓰는 채널. 진행 중이 아니면 null.
        /// 전송은 한 번에 하나만 돌고(SetTransferUi가 보장) 백그라운드 스레드에서 로그를
        /// 올리므로, 그 로그가 '작업을 시작한 모드'의 뷰어로 가도록 여기에 기억해 둔다.
        /// </summary>
        private LogCh? _opCh = null;

        // 시리얼 수신 줄 조립용 (FeedSerialRx 참고). DataReceived는 백그라운드 스레드다.
        private readonly StringBuilder _serialRxBuf = new StringBuilder();
        private readonly object _serialRxLock = new object();

        /// <summary>로그 한 줄의 종류. 태그 뒷부분(`[UART-RX]`의 RX)이 된다.</summary>
        private enum LogKind
        {
            Rx,      // 보드 → PC
            Tx,      // PC → 보드
            State,   // 전송 단계 (UI가 프로토콜 진행에서 파생)
            Info,    // 연결/파일 등 안내
            Err      // 오류
        }

        /// <summary>
        /// 태그 붙은 한 줄을 기록한다. `[03:10:56][UART-RX] ...` 형식.
        /// </summary>
        /// <param name="msg">줄 내용(줄바꿈 없이 넘길 것 — 여기서 붙인다)</param>
        private void Log(LogCh ch, LogKind kind, string msg)
        {
            // 채널 이름은 '출처'를 따른다. Auto면 지금 로그를 만드는 작업 기준으로 정해진다.
            LogCh resolved = (ch == LogCh.Auto)
                ? (_opCh ?? (RBtn_TransMode_Ethernet.Checked ? LogCh.Eth : LogCh.Serial))
                : ch;

            string chanName = (resolved == LogCh.Eth) ? "Ethernet" : "UART";
            string kindName;
            switch (kind)
            {
                case LogKind.Rx:    kindName = "RX";    break;
                case LogKind.Tx:    kindName = "TX";    break;
                case LogKind.State: kindName = "STATE"; break;
                case LogKind.Err:   kindName = "ERR";   break;
                default:            kindName = "INFO";  break;
            }

            AppendLog($"[{DateTime.Now:HH:mm:ss}][{chanName}-{kindName}] {msg}\r\n", resolved);
        }

        /// <summary>
        /// 로그를 출처에 맞는 뷰어에 안전하게 덧붙인다(태그 없이 원문 그대로).
        /// ⚠️ 화면에 보이는 모드로 고르면 안 된다 — 시리얼 데이터가 도착한 순간 사용자가
        ///    Ethernet을 보고 있으면 시리얼 로그가 Ethernet 뷰어에 섞인다.
        /// </summary>
        private void AppendLog(string text, LogCh ch = LogCh.Auto)
        {
            if (IsDisposed) return;
            if (InvokeRequired) { BeginInvoke(new Action(() => AppendLog(text, ch))); return; }

            if (ch == LogCh.Auto)
                ch = _opCh ?? (RBtn_TransMode_Ethernet.Checked ? LogCh.Eth : LogCh.Serial);

            var box = (ch == LogCh.Eth) ? Tbox_Ethernet_ReceivedLog : Tbox_RS232_ReceivedLog;
            if (!box.IsDisposed) box.AppendText(text);
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

        /// <summary>FW_APP open 버튼: 앱 실행영역(Staging 경유)으로 보낼 펌웨어를 로드.</summary>
        private void Btn_FW_Open_Click(object sender, EventArgs e)
        {
            LoadFirmwareInto(ref _fwImage, "FW_APP", textBox_FW_FIle, label_FW_Size);
        }

        /// <summary>FACTORY open 버튼: 골든 이미지로 넣을 펌웨어를 로드(앱과 별개 파일).</summary>
        private void Btn_FACTORY_Open_Click(object sender, EventArgs e)
        {
            LoadFirmwareInto(ref _factoryImage, "FACTORY", textBox_FACTORY_FIle, label3);
        }

        /// <summary>
        /// 파일 선택 → 파싱 → 해당 슬롯의 경로·크기 표시. 취소하면 기존 상태를 유지한다.
        /// </summary>
        /// <remarks>
        /// APP용이든 FACTORY용이든 <b>둘 다 APP_BASE_ADDRESS(0x08020000) 기준으로 파싱</b>한다.
        /// Factory에 넣는 이미지도 결국 롤백 시 부트로더가 App 실행영역으로 복사하는 것이므로,
        /// 링커가 0x08020000에 링크한 그 펌웨어여야 하기 때문이다. 기록 위치(Factory 슬롯)는
        /// MCU가 정하며 PC는 주소를 보내지 않는다.
        /// </remarks>
        private void LoadFirmwareInto(ref byte[] target, string slotName, TextBox pathBox, Label sizeLabel)
        {
            using (var dlg = new OpenFileDialog
            {
                Title = $"{slotName} 펌웨어 선택",
                Filter = "펌웨어 (*.hex;*.bin)|*.hex;*.bin|Intel HEX (*.hex)|*.hex|Binary (*.bin)|*.bin|모든 파일 (*.*)|*.*"
            })
            {
                if (dlg.ShowDialog() != DialogResult.OK) return;   // 취소 → 기존 로드 상태 유지

                try
                {
                    List<string> warnings = null;
                    if (dlg.FileName.EndsWith(".hex", StringComparison.OrdinalIgnoreCase))
                    {
                        target = ParseIntelHex(dlg.FileName, APP_BASE_ADDRESS, out warnings);
                    }
                    else
                    {
                        // .bin은 주소 정보가 없어 슬롯 시작에 그대로 올라간다고 가정한다.
                        // 검증은 .hex와 동일하게 걸어 잘못된 파일이 보드에 닿지 않게 한다.
                        target = PadTo4(File.ReadAllBytes(dlg.FileName));
                        ValidateImage(target);
                    }

                    if (warnings != null)
                        foreach (string w in warnings)
                            Log(LogCh.Auto, LogKind.Info, $"{slotName} 경고: {w}");

                    pathBox.Text = dlg.FileName;                          // 선택된 파일 경로 표시
                    sizeLabel.Text = $"{target.Length:N0} Byte";          // 파싱된 이미지 크기 표시
                    Log(LogCh.Auto, LogKind.Info, $"{slotName} 이미지 로드: {Path.GetFileName(dlg.FileName)} ({target.Length} bytes)");
                }
                catch (Exception ex)
                {
                    target = null;
                    pathBox.Text = "";
                    sizeLabel.Text = "0 Byte";
                    MessageBox.Show($"{slotName} 펌웨어 파싱 실패: " + ex.Message);
                }
            }
        }

        /// <summary>F/W Download 버튼: 앱 실행영역용 정상 OTA (Staging에 저장 후 보드가 재부팅·적용).</summary>
        private async void Btn_FW_Download_Click(object sender, EventArgs e)
        {
            await RunFirmwareTransfer(_fwImage, "FWUPDATE", "Staging", boardReboots: true);
        }

        /// <summary>
        /// FACTORY Download 버튼: 골든 이미지(Factory 슬롯)를 직접 교체한다.
        /// </summary>
        /// <remarks>
        /// Factory는 '롤백 복귀처'다. 여기에 다른 펌웨어를 넣어두면 롤백이 실제로 복원되는지
        /// 눈으로 확인할 수 있다. 다만 고장난 이미지를 넣으면 롤백이 고장난 펌웨어를 복구하게
        /// 되므로(ST-Link로만 회복 가능) 실행 전에 한 번 확인을 받는다.
        /// 이 경로는 메타데이터를 건드리지 않고 보드도 재부팅하지 않는다.
        /// </remarks>
        private async void Btn_FACTORY_Download_Click(object sender, EventArgs e)
        {
            var answer = MessageBox.Show(
                "Factory(골든 이미지) 슬롯을 덮어씁니다.\n\n" +
                "Factory는 롤백 시 복구되는 원본입니다.\n" +
                "고장난 펌웨어를 쓰면 롤백해도 고장난 상태로 복구되며\n" +
                "ST-Link로만 회복할 수 있습니다.\n\n" +
                "계속하시겠습니까?",
                "FACTORY 슬롯 덮어쓰기 확인",
                MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2);

            if (answer != DialogResult.Yes) return;

            await RunFirmwareTransfer(_factoryImage, "FWFACTRY", "Factory", boardReboots: false);
        }

        // ============================================================
        //  슬롯 전환 (펌웨어 전송 없이 App 실행영역의 내용만 바꾼다)
        // ============================================================

        /// <summary>
        /// FACTORY로 전환: 보드가 다음 부팅에서 Factory→App 복원을 수행하게 한다.
        /// </summary>
        /// <remarks>
        /// 자동 롤백은 '앱이 자가확인에 실패(=사실상 멈춤)'했을 때만 발동한다.
        /// 앱이 살아서 통신은 되는데 기능이 잘못된 경우엔 워치독이 개입하지 않으므로
        /// 이 버튼으로 직접 되돌린다. 펌웨어를 다시 보내지 않으므로 즉시 완료된다.
        /// </remarks>
        private async void Btn_ToFACTORY_Click(object sender, EventArgs e)
        {
            var answer = MessageBox.Show(
                "Factory 이미지로 되돌립니다.\n\n" +
                "보드가 재부팅되며 App 실행영역이 Factory 내용으로 덮어써집니다.\n" +
                "되돌아오려면 FW_APP 쪽 [open] → [F/W Download] 로 다시 업로드하세요.\n\n" +
                "계속하시겠습니까?",
                "FACTORY로 전환",
                MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2);

            if (answer != DialogResult.Yes) return;

            await RunSlotSwitch("FWROLLBK", null, "Factory");
        }

        /// <summary>
        /// 슬롯 전환 명령을 보내고 DONE/FAILED 응답을 처리한다(공통).
        /// </summary>
        /// <param name="header">명령 뒤에 붙일 8바이트 헤더. 없으면 null.</param>
        private async Task RunSlotSwitch(string command, byte[] header, string targetName)
        {
            bool eth = RBtn_TransMode_Ethernet.Checked;

            IFwLink link;
            if (eth)
            {
                if (_tcp == null || !_tcp.IsConnected)
                {
                    MessageBox.Show("먼저 Socket Connect로 보드에 연결하세요.");
                    return;
                }
                link = _tcp;
            }
            else
            {
                if (!_serial.IsOpen)
                {
                    MessageBox.Show("먼저 COM 포트를 여세요 (OPEN).");
                    return;
                }
                link = _serialLink;
            }

            SetTransferUi(true);
            _opCh = eth ? LogCh.Eth : LogCh.Serial;   // 작업 로그를 시작한 모드의 뷰어로 고정
            string result;
            try     { result = await Task.Run(() => SendSlotSwitch(command, header, link)); }
            finally { _opCh = null; }
            SetTransferUi(false);

            // 성공하면 보드가 재부팅된다 → Ethernet 연결은 끊어지므로 정리.
            if (eth && result == "DONE")
            {
                _tcp?.Dispose();
                _tcp = null;
                UpdateSocketConnState(false);
                Log(LogCh.Eth, LogKind.Info, "보드 재부팅 중 — 다시 쓰려면 재접속(Connect) 하세요.");
            }

            if (result == "DONE")
            {
                MessageBox.Show($"{targetName} 로 전환합니다.\n보드가 재부팅되어 적용됩니다.");
            }
            else if (result == "FAILED")
            {
                MessageBox.Show(
                    $"보드가 전환을 거절했습니다 ({targetName}).\n\n" +
                    "대상 슬롯이 비어 있거나 내용이 일치하지 않습니다.\n" +
                    "보드는 그대로 동작 중이니 안전합니다 — 로그를 확인하세요.");
            }
            else
            {
                MessageBox.Show("응답이 없습니다 — 연결 상태와 로그를 확인하세요.");
            }
        }

        /// <summary>슬롯 전환 명령 송신 + 응답 대기 (백그라운드 스레드).</summary>
        private string SendSlotSwitch(string command, byte[] header, IFwLink link)
        {
            link.PauseAsyncRx();
            try
            {
                link.ReadTimeout = 5000;
                link.DiscardInBuffer();

                link.Write(command);
                if (header != null) link.Write(header, 0, header.Length);
                Log(LogCh.Auto, LogKind.Tx, command);
                Log(LogCh.Auto, LogKind.State, "State - Slot switch requested");

                // MCU는 재부팅 전에 대상 슬롯을 검증하므로 응답까지 잠깐 걸릴 수 있다.
                string res = WaitForAnyText(new[] { "DONE", "FAILED" }, 5000, link);
                Log(LogCh.Auto, LogKind.Rx, res ?? "(무응답)");
                if (res == "DONE") Log(LogCh.Auto, LogKind.State, "State - System will RST");
                return res;
            }
            catch (TimeoutException) { Log(LogCh.Auto, LogKind.Err, "타임아웃"); return null; }
            catch (Exception ex) { Log(LogCh.Auto, LogKind.Err, ex.Message); return null; }
            finally
            {
                link.ResumeAsyncRx();
            }
        }

        /// <summary>
        /// 두 다운로드 버튼의 공통 처리: 링크 선택 → 전송 → 뒷정리.
        /// </summary>
        /// <param name="command">MCU에 보낼 8바이트 명령("FWUPDATE" 또는 "FWFACTRY")</param>
        /// <param name="targetName">사용자 안내용 대상 이름</param>
        /// <param name="boardReboots">
        /// 전송 성공 후 보드가 재부팅하는지 여부. Staging(정상 OTA)은 재부팅하므로 TCP 연결이
        /// 끊기지만, Factory 교체는 재부팅하지 않으므로 연결을 그대로 유지해야 한다.
        /// </param>
        private async Task RunFirmwareTransfer(byte[] image, string command, string targetName, bool boardReboots)
        {
            bool eth = RBtn_TransMode_Ethernet.Checked;

            IFwLink link;
            if (eth)
            {
                if (_tcp == null || !_tcp.IsConnected)
                {
                    MessageBox.Show("먼저 Socket Connect로 보드에 연결하세요.");
                    return;
                }
                link = _tcp;
            }
            else
            {
                if (!_serial.IsOpen)
                {
                    MessageBox.Show("먼저 COM 포트를 여세요 (OPEN).");
                    return;
                }
                link = _serialLink;
            }

            if (image == null)
            {
                MessageBox.Show($"먼저 {targetName} 쪽 [open] 버튼으로 펌웨어 파일을 선택하세요.");
                return;
            }

            SetTransferUi(true);
            _opCh = eth ? LogCh.Eth : LogCh.Serial;   // 작업 로그를 시작한 모드의 뷰어로 고정
            bool ok;
            try     { ok = await Task.Run(() => SendFirmwareSequence(image, link, command)); }
            finally { _opCh = null; }
            SetTransferUi(false);

            // Ethernet + 보드가 재부팅하는 경우에만 연결을 정리한다.
            // (Factory 교체는 보드가 계속 돌고 있으므로 소켓을 그대로 두어야 재접속 없이 이어서 쓸 수 있다)
            if (eth && boardReboots)
            {
                _tcp?.Dispose();
                _tcp = null;
                UpdateSocketConnState(false);
                if (ok) Log(LogCh.Eth, LogKind.Info, "보드 재부팅 중 — 다시 전송하려면 재접속(Connect) 하세요.");
            }

            if (ok)
            {
                MessageBox.Show(boardReboots
                    ? $"펌웨어 전송 완료! ({targetName}에 저장 → 보드가 재부팅되어 적용됩니다)"
                    : $"{targetName} 이미지 교체 완료! (재부팅 없음 — 롤백 시 이 이미지로 복구됩니다)");
            }
            else
            {
                MessageBox.Show($"{targetName} 전송 실패 — 로그를 확인하세요.");
            }
        }

        /// <summary>
        /// 프로토콜에 따라 펌웨어를 전송한다 (백그라운드 스레드). 전송 계층은 link로 추상화.
        /// </summary>
        /// <param name="command">
        /// 대상 슬롯을 정하는 8바이트 명령. 프로토콜 본문은 완전히 동일하고 MCU가 기록 위치만 바꾼다.
        ///   "FWUPDATE" → Staging (정상 OTA: 적용 후 보드가 재부팅)
        ///   "FWFACTRY" → Factory (골든 이미지 교체: 재부팅 없음)
        /// </param>
        private bool SendFirmwareSequence(byte[] image, IFwLink link, string command)
        {
            const byte ACK = 0x79, NACK = 0x1F;
            const int CHUNK = 256;

            link.PauseAsyncRx();                           // 동기 읽기를 위해 비동기 수신 중지
            try
            {
                link.ReadTimeout = 10000;                  // erase가 오래 걸릴 수 있어 넉넉히
                link.DiscardInBuffer();

                // 1) 명령 전송 (대상 슬롯 결정)
                Log(LogCh.Auto, LogKind.State, "State - Updating");
                link.Write(command);
                Log(LogCh.Auto, LogKind.Tx, command);

                // 2) READY 대기
                if (!WaitForText("READY", 3000, link))
                {
                    Log(LogCh.Auto, LogKind.Err, "READY 응답 없음 (보드가 초록 하트비트 상태인지 확인)");
                    return false;
                }
                Thread.Sleep(30);
                link.DiscardInBuffer();                    // "READY\r\n"의 \r\n 제거
                Log(LogCh.Auto, LogKind.Rx, "READY");

                // 3) 헤더 전송: [전체크기 4B][CRC32 4B] (little-endian)
                uint crc = Crc32Stm32(image);
                var header = new byte[8];
                BitConverter.GetBytes((uint)image.Length).CopyTo(header, 0);
                BitConverter.GetBytes(crc).CopyTo(header, 4);
                link.Write(header, 0, 8);
                Log(LogCh.Auto, LogKind.Tx, $"size={image.Length}, CRC32=0x{crc:X8}");

                // 4) Staging erase 후 ACK — 보드가 섹터를 지우는 동안이라 수 초 걸릴 수 있다.
                Log(LogCh.Auto, LogKind.State, "State - Erasing (보드가 대상 슬롯 소거 중)");
                int b = link.ReadByte();
                if (b != ACK) { Log(LogCh.Auto, LogKind.Err, $"erase ACK 실패 (0x{b:X2})"); return false; }
                Log(LogCh.Auto, LogKind.Rx, $"ACK — 소거 완료, 전송 시작 ({image.Length} bytes)");
                Log(LogCh.Auto, LogKind.State, "State - Saving Stage");

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
                        link.Write(image, sent, n);         // 데이터

                        // [임시 테스트] 처음 3개 청크(offset 0/256/512)의 첫 시도에만 CRC를 깨서 재전송 유발
                        byte[] txCrc = crcBytes;
                        if (_debugCorruptFirstAttempt && retries == 0 &&
                            (sent == 0 || sent == 256 || sent == 512))
                        {
                            txCrc = new byte[] { (byte)(crcBytes[0] ^ 0xFF), crcBytes[1] };
                        }
                        link.Write(txCrc, 0, 2);            // CRC16 (little-endian)

                        int resp = link.ReadByte();
                        if (resp == ACK) break;             // 확정 → 다음 청크
                        if (resp == NACK)
                        {
                            if (++retries > 5)
                            {
                                Log(LogCh.Auto, LogKind.Err, $"청크 재시도 초과 @ offset {sent}");
                                return false;
                            }
                            totalRetries++;
                            Log(LogCh.Auto, LogKind.Err, $"청크 CRC 불일치 → 재전송 @ {sent} (retry {retries})");
                            continue;                       // 같은 청크 재전송
                        }
                        Log(LogCh.Auto, LogKind.Err, $"예상치 못한 응답 0x{resp:X2} @ offset {sent}");
                        return false;
                    }

                    sent += n;
                    UpdateProgress(sent, image.Length);
                }
                if (totalRetries > 0) Log(LogCh.Auto, LogKind.Info, $"청크 재전송 총 {totalRetries}회 (자동 복구됨)");

                // 6) MCU가 Staging CRC를 검증한 결과 대기 (DONE 또는 CRCERR)
                Log(LogCh.Auto, LogKind.State, "State - Verifying (보드가 전체 CRC32 검증 중)");
                string res = WaitForAnyText(new[] { "DONE", "CRCERR" }, 5000, link);
                if (res == "CRCERR")
                {
                    Log(LogCh.Auto, LogKind.Rx, "CRCERR");
                    Log(LogCh.Auto, LogKind.Err, "전체 CRC 불일치 — 전송이 손상됨 (적용되지 않음)");
                    return false;
                }
                if (res != "DONE") { Log(LogCh.Auto, LogKind.Err, "완료 응답 없음"); return false; }

                Log(LogCh.Auto, LogKind.Rx, "DONE — CRC 검증 통과");
                Log(LogCh.Auto, LogKind.State, "State - Update Done");

                // FWFACTRY(Factory 직접 쓰기)는 재부팅하지 않는다. 그 구분은 호출자가 안다.
                if (command == "FWUPDATE")
                    Log(LogCh.Auto, LogKind.State, "State - System will RST");

                return true;
            }
            catch (TimeoutException) { Log(LogCh.Auto, LogKind.Err, "타임아웃"); return false; }
            catch (Exception ex) { Log(LogCh.Auto, LogKind.Err, ex.Message); return false; }
            finally
            {
                link.ResumeAsyncRx();   // 비동기 수신 복원(TCP는 no-op)
            }
        }

        /// <summary>target 문자열이 나올 때까지 동기 수신 (timeout 초과 시 false).</summary>
        private bool WaitForText(string target, int timeoutMs, IFwLink link)
        {
            int old = link.ReadTimeout;
            link.ReadTimeout = timeoutMs;
            var sb = new StringBuilder();
            try
            {
                while (true)
                {
                    sb.Append((char)link.ReadByte());
                    if (sb.ToString().Contains(target)) return true;
                }
            }
            catch (TimeoutException) { return false; }
            finally { link.ReadTimeout = old; }
        }

        /// <summary>여러 target 중 하나가 나올 때까지 동기 수신. 매칭된 문자열 반환(타임아웃 시 null).</summary>
        private string WaitForAnyText(string[] targets, int timeoutMs, IFwLink link)
        {
            int old = link.ReadTimeout;
            link.ReadTimeout = timeoutMs;
            var sb = new StringBuilder();
            try
            {
                while (true)
                {
                    sb.Append((char)link.ReadByte());
                    string s = sb.ToString();
                    foreach (string t in targets)
                        if (s.Contains(t)) return t;
                }
            }
            catch (TimeoutException) { return null; }
            finally { link.ReadTimeout = old; }
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
                Log(LogCh.Auto, LogKind.Tx, $"{pct,3}%  {sent:N0}/{total:N0} bytes  {kbps:F1} KB/s");
            }
        }

        /// <summary>전송 중 UI 잠금/해제.</summary>
        private void SetTransferUi(bool active)
        {
            if (InvokeRequired) { BeginInvoke(new Action(() => SetTransferUi(active))); return; }
            Btn_FW_Open.Enabled = !active;
            Btn_FW_Download.Enabled = !active;
            Btn_FACTORY_Open.Enabled = !active;
            Btn_FACTORY_Download.Enabled = !active;
            Btn_ToFACTORY.Enabled = !active;
            Btn_COMOPEN.Enabled = !active && !_serial.IsOpen;
            Btn_COMCLOSE.Enabled = !active && _serial.IsOpen;

            bool tcpConn = _tcp != null && _tcp.IsConnected;
            Btn_SOCKETConnect.Enabled = !active && !tcpConn;
            Btn_SOCKETDisconnect.Enabled = !active && tcpConn;

            // 전송 중에는 모드 전환을 막는다. ApplyTransferMode가 '쓰지 않는 쪽' 연결을
            // 닫아버리므로, 전송 도중 모드를 바꾸면 그 전송이 쓰던 포트/소켓이 닫힌다.
            RBtn_TransMode_RS232.Enabled = !active;
            RBtn_TransMode_Ethernet.Enabled = !active;

            // 전송 중에는 상태 폴링을 멈춘다. 폴링과 OTA는 같은 소켓을 쓰므로 겹치면
            // 청크/ACK 스트림에 FWINFO?? 요청이 끼어들어 전송이 깨진다.
            // (이 메서드는 Task.Run으로 전송을 시작하기 '전에' UI 스레드에서 불리고,
            //  타이머 Tick도 UI 스레드라 서로 끼어들 수 없다 → 경합 없음)
            if (active) _infoTimer.Stop();
            else if (tcpConn) _infoTimer.Start();

            if (!active) TStripProBar_SendState.Value = 0;
        }

        // ============================================================
        //  상태 폴링 (Ethernet 전용)
        //
        //  RS-232는 보드가 1초마다 배너를 스스로 뿜으므로 뷰어에 그대로 쌓인다.
        //  TCP는 보드가 자발적으로 보내면 OTA 청크 스트림에 끼어들어 프로토콜이 깨지므로,
        //  '요청이 있을 때만' 응답하게 하고 폴링 주기는 PC가 쥔다. 화면상 결과는 동일하다.
        // ============================================================

        /// <summary>보드에 FWINFO??를 보내 상태 한 줄을 받아 뷰어에 출력한다.</summary>
        /// <summary>
        /// 접속 직후 1회: 보드에 FWSYS??? 를 보내 장치 정보 + 메모리 사용량을 받아 찍는다.
        /// </summary>
        /// <remarks>
        /// 응답은 여러 줄이고 종료 표시가 따로 없다. 그래서 '한 줄 읽고 짧은 타임아웃으로
        /// 더 안 오면 끝'으로 판정한다 — 보드가 연속으로 즉시 보내므로 실제로는 첫 타임아웃이
        /// 곧 끝을 뜻한다. 길이를 헤더로 주는 방식이 더 견고하지만, 이 명령은 사람이 읽는
        /// 진단용이라 프로토콜을 늘리지 않았다.
        /// </remarks>
        private void QuerySysInfo(IFwLink link, LogCh ch)
        {
            if (link == null || !link.IsConnected) return;

            try
            {
                link.PauseAsyncRx();                 // 시리얼이면 비동기 뷰어가 가로채지 않도록
                link.DiscardInBuffer();
                link.ReadTimeout = 1000;             // 첫 줄은 넉넉히 기다린다
                link.Write("FWSYS???");

                var sb = new StringBuilder(128);
                int lines = 0;

                while (lines < 32)                   // 폭주 방어
                {
                    int b;
                    try { b = link.ReadByte(); }
                    catch (TimeoutException) { break; }   // 더 안 옴 = 보고 끝

                    if (b == '\n')
                    {
                        if (sb.Length > 0) { Log(ch, LogKind.Rx, sb.ToString()); lines++; sb.Clear(); }
                        link.ReadTimeout = 300;      // 이후 줄은 짧게 — 끝을 빨리 판정
                        continue;
                    }
                    if (b != '\r') sb.Append((char)b);
                }

                if (sb.Length > 0) Log(ch, LogKind.Rx, sb.ToString());
                if (lines == 0) Log(ch, LogKind.Err, "FWSYS??? 응답 없음 (보드 펌웨어가 이 명령을 모를 수 있음)");
            }
            catch (Exception ex)
            {
                Log(ch, LogKind.Err, "시스템 정보 조회 실패: " + ex.Message);
            }
            finally
            {
                link.ResumeAsyncRx();
            }
        }

        private void PollFirmwareInfo()
        {
            var link = _tcp;
            if (link == null || !link.IsConnected) { _infoTimer.Stop(); return; }

            try
            {
                link.DiscardInBuffer();
                link.ReadTimeout = 300;      // 보드는 즉답한다. UI 스레드를 오래 잡지 않는다.
                link.Write("FWINFO??");

                var sb = new StringBuilder(96);
                for (int i = 0; i < 128; i++)
                {
                    int b = link.ReadByte();
                    if (b == '\n') break;
                    if (b != '\r') sb.Append((char)b);
                }
                if (sb.Length > 0) Log(LogCh.Eth, LogKind.Rx, sb.ToString());
            }
            catch (TimeoutException)
            {
                // 응답 없음 — 이번 주기는 건너뛴다. 보드가 바쁠 수 있으므로 연결은 유지.
            }
            catch (Exception ex)
            {
                // 소켓이 끊긴 경우 — 폴링을 멈추고 연결 상태를 실제와 맞춘다.
                Log(LogCh.Eth, LogKind.Err, "상태 조회 실패: " + ex.Message);
                _infoTimer.Stop();
                _tcp?.Dispose();
                _tcp = null;
                UpdateSocketConnState(false);
            }
        }

        // ============================================================
        //  소켓(Ethernet) 연결
        // ============================================================

        /// <summary>Connect 버튼: Tbox_IPAddress:Tbox_Port로 보드(TCP 서버)에 접속.</summary>
        private void Btn_SOCKETConnect_Click(object sender, EventArgs e)
        {
            if (_tcp != null && _tcp.IsConnected) return;

            string ip = Tbox_IPAddress.Text.Trim();
            if (!int.TryParse(Tbox_Port.Text.Trim(), out int port) || port < 1 || port > 65535)
            {
                MessageBox.Show("포트 번호가 올바르지 않습니다 (1~65535).");
                return;
            }

            try
            {
                _tcp = new TcpLink(ip, port);
                UpdateSocketConnState(true);

                PollFirmwareInfo();      // 접속 직후 1회 — 첫 표시까지 1초를 기다리지 않도록
                _infoTimer.Start();
                Log(LogCh.Eth, LogKind.Info, $"CONNECT {ip}:{port} 연결됨");
                QuerySysInfo(_tcp, LogCh.Eth);          // 보드/메모리 정보 1회
                Log(LogCh.Eth, LogKind.State, "State - Normal");
            }
            catch (Exception ex)
            {
                _tcp?.Dispose();
                _tcp = null;
                UpdateSocketConnState(false);
                MessageBox.Show("소켓 연결 실패: " + ex.Message);
            }
        }

        /// <summary>Disconnect 버튼: TCP 연결을 닫는다.</summary>
        private void Btn_SOCKETDisconnect_Click(object sender, EventArgs e)
        {
            _infoTimer.Stop();
            _tcp?.Dispose();
            _tcp = null;
            UpdateSocketConnState(false);
            Log(LogCh.Eth, LogKind.Info, "DISCONNECT");
        }

        /// <summary>소켓 연결 상태에 따라 Connect/Disconnect 버튼과 상태 표시줄을 갱신한다.</summary>
        private void UpdateSocketConnState(bool connected)
        {
            if (InvokeRequired) { BeginInvoke(new Action(() => UpdateSocketConnState(connected))); return; }
            Btn_SOCKETConnect.Enabled = !connected;
            Btn_SOCKETDisconnect.Enabled = connected;
            ProgressBar_SocketState.Maximum = 100;
            ProgressBar_SocketState.Value = connected ? 100 : 0;   // 연결됨=가득 참(상태 표시)
        }

        /// <summary>Intel HEX 파일을 파싱해 baseAddress 기준 연속 바이너리로 변환한다.</summary>
        /// <param name="warnings">무시된 레코드 등 사용자에게 알릴 사항(치명적이지 않은 것)이 담긴다.</param>
        /// <remarks>
        /// 전체 CRC32는 <b>전송 경로</b>만 보호한다. 파일 자체가 깨진 경우(디스크/네트워크 손상)는
        /// 레코드 체크섬만이 유일한 방어선이므로 반드시 검증한다 — 이걸 건너뛰면 깨진 이미지로
        /// 계산한 CRC32가 MCU에서 그대로 통과해 손상된 펌웨어가 "DONE"으로 적용된다.
        ///
        /// 슬롯 범위 밖 레코드는 <b>오류가 아니라 무시</b>한다. ST 툴체인이 옵션바이트·OTP 영역
        /// (0x1FFF_xxxx) 레코드를 붙이는 일이 드물지 않은데, 이걸 이미지에 포함시키면 크기가
        /// 수백 MB로 폭주하고 MCU가 헤더를 NACK → Wdg_Panic → <b>멀쩡한 보드가 강제 롤백</b>된다.
        /// </remarks>
        private static byte[] ParseIntelHex(string path, uint baseAddress, out List<string> warnings)
        {
            var records = new List<KeyValuePair<uint, byte[]>>();
            uint addrOffset = 0;             // type 04(선형) 또는 type 02(세그먼트)가 정하는 주소 상위분
            uint minAddr = uint.MaxValue;
            uint maxEnd = baseAddress;
            int lineNo = 0;
            int ignored = 0;
            bool sawEof = false;
            warnings = new List<string>();

            uint slotEnd = baseAddress + SLOT_SIZE;   // 경계(미포함)

            foreach (string raw in File.ReadAllLines(path))
            {
                lineNo++;
                string line = raw.Trim();
                if (line.Length == 0) continue;
                if (line[0] != ':')
                    throw new Exception($"{lineNo}번째 줄이 ':'로 시작하지 않습니다.");
                if (line.Length < 11)
                    throw new Exception($"{lineNo}번째 줄이 너무 짧습니다({line.Length}자).");

                int len = Convert.ToInt32(line.Substring(1, 2), 16);

                // 길이 필드와 실제 줄 길이의 일치 확인 (':' + len·addr·type 8자 + 데이터 + 체크섬 2자)
                if (line.Length != 11 + len * 2)
                    throw new Exception($"{lineNo}번째 줄의 길이가 맞지 않습니다. len={len}이면 {11 + len * 2}자여야 하는데 {line.Length}자입니다.");

                // 레코드 체크섬: 모든 바이트(체크섬 포함)의 합의 하위 8비트가 0이어야 한다
                int sum = 0;
                for (int i = 1; i + 1 < line.Length; i += 2)
                    sum += Convert.ToInt32(line.Substring(i, 2), 16);
                if ((sum & 0xFF) != 0)
                    throw new Exception($"{lineNo}번째 줄의 체크섬이 틀렸습니다. 파일이 손상되었습니다.");

                int addr16 = Convert.ToInt32(line.Substring(3, 4), 16);
                int type = Convert.ToInt32(line.Substring(7, 2), 16);
                string dataHex = line.Substring(9, len * 2);

                switch (type)
                {
                    case 0x00:                                  // 데이터
                        {
                            uint abs = addrOffset + (uint)addr16;
                            uint end = abs + (uint)len;         // 경계(미포함)

                            if (end <= baseAddress || abs >= slotEnd)
                            {
                                ignored++;                      // 슬롯 밖(옵션바이트 등) → 조용히 제외
                                break;
                            }
                            if (abs < baseAddress || end > slotEnd)
                                throw new Exception(
                                    $"{lineNo}번째 줄의 레코드(0x{abs:X8}~0x{end - 1:X8})가 " +
                                    $"슬롯 경계(0x{baseAddress:X8}~0x{slotEnd - 1:X8})를 걸칩니다.");

                            var data = new byte[len];
                            for (int i = 0; i < len; i++)
                                data[i] = Convert.ToByte(dataHex.Substring(i * 2, 2), 16);
                            records.Add(new KeyValuePair<uint, byte[]>(abs, data));
                            if (abs < minAddr) minAddr = abs;
                            if (end > maxEnd) maxEnd = end;
                            break;
                        }

                    case 0x02:                                  // 확장 세그먼트 주소 (<<4, <<16이 아님)
                        if (len != 2) throw new Exception($"{lineNo}번째 줄: type 02의 길이는 2여야 합니다.");
                        addrOffset = (uint)Convert.ToInt32(dataHex, 16) << 4;
                        break;

                    case 0x04:                                  // 확장 선형 주소 (상위 16비트)
                        if (len != 2) throw new Exception($"{lineNo}번째 줄: type 04의 길이는 2여야 합니다.");
                        addrOffset = (uint)Convert.ToInt32(dataHex, 16) << 16;
                        break;

                    case 0x03:                                  // 시작 세그먼트 주소 — 진입점은 MCU가 벡터에서 얻는다
                    case 0x05:                                  // 시작 선형 주소 — 위와 같음
                        break;

                    case 0x01:                                  // EOF
                        sawEof = true;
                        break;

                    default:
                        throw new Exception($"{lineNo}번째 줄: 알 수 없는 레코드 타입 0x{type:X2}입니다.");
                }

                if (sawEof) break;
            }

            if (!sawEof) throw new Exception("EOF 레코드(:00000001FF)가 없습니다. 파일이 잘렸을 수 있습니다.");
            if (records.Count == 0) throw new Exception("슬롯 범위 안에 데이터 레코드가 없습니다.");

            if (ignored > 0)
                warnings.Add($"슬롯(0x{baseAddress:X8}~0x{slotEnd - 1:X8}) 밖의 레코드 {ignored}개를 제외했습니다.");

            // 이미지는 반드시 슬롯 시작에서 출발해야 한다. 앞이 비어 있으면 그 자리가 0xFF로 채워지고,
            // 선두 4바이트(초기 SP)가 0xFFFFFFFF가 되어 부트로더가 무효 판정 → 롤백으로 이어진다.
            if (minAddr != baseAddress)
                throw new Exception(
                    $"이미지가 0x{minAddr:X8}에서 시작합니다. 슬롯 시작(0x{baseAddress:X8})과 달라 " +
                    "링커 스크립트의 FLASH ORIGIN을 확인해야 합니다.");

            uint size = (maxEnd - baseAddress + 3u) & ~3u;   // 4바이트 정렬
            var image = new byte[size];
            for (int i = 0; i < image.Length; i++) image[i] = 0xFF;   // 빈 공간 0xFF

            var written = new bool[size];
            foreach (var rec in records)
            {
                int off = (int)(rec.Key - baseAddress);
                for (int i = 0; i < rec.Value.Length; i++)
                {
                    if (written[off + i])
                        throw new Exception($"주소 0x{rec.Key + (uint)i:X8}를 두 레코드가 중복해서 정의합니다.");
                    written[off + i] = true;
                }
                Array.Copy(rec.Value, 0, image, off, rec.Value.Length);
            }

            ValidateImage(image);
            return image;
        }

        /// <summary>
        /// 전송 전에 이미지가 이 슬롯에서 실제로 부팅 가능한 모양인지 확인한다.
        /// 부트로더 BL_JumpToApplication() / 앱 Ota_RequestRollback()과 같은 판정 기준을 쓴다.
        /// </summary>
        private static void ValidateImage(byte[] image)
        {
            if (image.Length == 0)
                throw new Exception("이미지가 비어 있습니다.");
            if (image.Length > SLOT_SIZE)
                throw new Exception($"이미지 크기 {image.Length:N0} 바이트가 슬롯 크기 {SLOT_SIZE:N0} 바이트를 넘습니다.");
            if (image.Length < 8)
                throw new Exception("이미지가 너무 작습니다(벡터 테이블 최소 8바이트).");

            uint sp = BitConverter.ToUInt32(image, 0);
            if (sp < RAM_START || sp > RAM_END)
                throw new Exception(
                    $"선두 4바이트(초기 스택 포인터)가 0x{sp:X8}로 RAM 범위" +
                    $"(0x{RAM_START:X8}~0x{RAM_END:X8}) 밖입니다. 이 슬롯용으로 링크된 펌웨어가 아닙니다.");
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
