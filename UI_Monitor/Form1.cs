using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UI_Monitor
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();

            // 시작 시 기본 모드(RS-232)에 맞춰 화면 상태를 한 번 맞춰준다.
            ApplyTransferMode();
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
    }
}
