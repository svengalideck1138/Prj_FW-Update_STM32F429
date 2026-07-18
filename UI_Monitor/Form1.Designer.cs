namespace UI_Monitor
{
    partial class Form1
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.GroupBox_TransferMode = new System.Windows.Forms.GroupBox();
            this.RBtn_TransMode_Ethernet = new System.Windows.Forms.RadioButton();
            this.RBtn_TransMode_RS232 = new System.Windows.Forms.RadioButton();
            this.GroupBox_ComportSetting = new System.Windows.Forms.GroupBox();
            this.Btn_COMRefresh = new System.Windows.Forms.Button();
            this.ProgressBar_ComportState = new System.Windows.Forms.ProgressBar();
            this.Btn_COMCLOSE = new System.Windows.Forms.Button();
            this.Btn_COMOPEN = new System.Windows.Forms.Button();
            this.Chk_CD = new System.Windows.Forms.CheckBox();
            this.Chk_DSR = new System.Windows.Forms.CheckBox();
            this.Chk_CTS = new System.Windows.Forms.CheckBox();
            this.Chk_RTS = new System.Windows.Forms.CheckBox();
            this.Chk_DTR = new System.Windows.Forms.CheckBox();
            this.Lb_ParityBits = new System.Windows.Forms.Label();
            this.Lb_StopBits = new System.Windows.Forms.Label();
            this.CBoxParityBits = new System.Windows.Forms.ComboBox();
            this.CBoxStopBits = new System.Windows.Forms.ComboBox();
            this.CBoxDataBits = new System.Windows.Forms.ComboBox();
            this.Lb_DataBits = new System.Windows.Forms.Label();
            this.CBoxBaudRate = new System.Windows.Forms.ComboBox();
            this.Lb_BaudRate = new System.Windows.Forms.Label();
            this.CBoxCOMPORT = new System.Windows.Forms.ComboBox();
            this.Lb_ComPort = new System.Windows.Forms.Label();
            this.GroupBox_SocketSetting = new System.Windows.Forms.GroupBox();
            this.ProgressBar_SocketState = new System.Windows.Forms.ProgressBar();
            this.Btn_SOCKETDisconnect = new System.Windows.Forms.Button();
            this.Tbox_Port = new System.Windows.Forms.TextBox();
            this.label7 = new System.Windows.Forms.Label();
            this.Btn_SOCKETConnect = new System.Windows.Forms.Button();
            this.Tbox_IPAddress = new System.Windows.Forms.TextBox();
            this.label11 = new System.Windows.Forms.Label();
            this.StatusStrip1 = new System.Windows.Forms.StatusStrip();
            this.TStripProBar_SendState = new System.Windows.Forms.ToolStripProgressBar();
            this.TSStatusLb_Percent = new System.Windows.Forms.ToolStripStatusLabel();
            this.TSStatusLb_nCounter = new System.Windows.Forms.ToolStripStatusLabel();
            this.GroupBox_ViewerRS232 = new System.Windows.Forms.GroupBox();
            this.RadBtn_ASC = new System.Windows.Forms.RadioButton();
            this.RadBtn_Byte = new System.Windows.Forms.RadioButton();
            this.Btn_Clear_RS232Log = new System.Windows.Forms.Button();
            this.Tbox_RS232_ReceivedLog = new System.Windows.Forms.TextBox();
            this.GroupBox_ViewerEthernet = new System.Windows.Forms.GroupBox();
            this.RadBtn_ASC_Eth = new System.Windows.Forms.RadioButton();
            this.RadBtn_Byte_Eth = new System.Windows.Forms.RadioButton();
            this.Btn_Clear_EthernetLog = new System.Windows.Forms.Button();
            this.Tbox_Ethernet_ReceivedLog = new System.Windows.Forms.TextBox();
            this.Btn_FW_Open = new System.Windows.Forms.Button();
            this.textBox_FW_FIle = new System.Windows.Forms.TextBox();
            this.label_FW_Size = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.groupBox_FWUpdate = new System.Windows.Forms.GroupBox();
            this.Btn_FW_Download = new System.Windows.Forms.Button();
            this.Btn_FACTORY_Download = new System.Windows.Forms.Button();
            this.label1 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.textBox_FACTORY_FIle = new System.Windows.Forms.TextBox();
            this.Btn_FACTORY_Open = new System.Windows.Forms.Button();
            this.textBox_FACT_Address = new System.Windows.Forms.TextBox();
            this.textBox_APP_Address = new System.Windows.Forms.TextBox();
            this.Btn_ToFACTORY = new System.Windows.Forms.Button();
            this.GroupBox_TransferMode.SuspendLayout();
            this.GroupBox_ComportSetting.SuspendLayout();
            this.GroupBox_SocketSetting.SuspendLayout();
            this.StatusStrip1.SuspendLayout();
            this.GroupBox_ViewerRS232.SuspendLayout();
            this.GroupBox_ViewerEthernet.SuspendLayout();
            this.groupBox_FWUpdate.SuspendLayout();
            this.SuspendLayout();
            // 
            // GroupBox_TransferMode
            // 
            this.GroupBox_TransferMode.Controls.Add(this.RBtn_TransMode_Ethernet);
            this.GroupBox_TransferMode.Controls.Add(this.RBtn_TransMode_RS232);
            this.GroupBox_TransferMode.Font = new System.Drawing.Font("Gulim", 9F, ((System.Drawing.FontStyle)((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Underline))), System.Drawing.GraphicsUnit.Point, ((byte)(129)));
            this.GroupBox_TransferMode.Location = new System.Drawing.Point(12, 12);
            this.GroupBox_TransferMode.Name = "GroupBox_TransferMode";
            this.GroupBox_TransferMode.Size = new System.Drawing.Size(153, 43);
            this.GroupBox_TransferMode.TabIndex = 2;
            this.GroupBox_TransferMode.TabStop = false;
            this.GroupBox_TransferMode.Text = "Transfer Mode";
            // 
            // RBtn_TransMode_Ethernet
            // 
            this.RBtn_TransMode_Ethernet.AutoSize = true;
            this.RBtn_TransMode_Ethernet.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.RBtn_TransMode_Ethernet.Location = new System.Drawing.Point(62, 18);
            this.RBtn_TransMode_Ethernet.Name = "RBtn_TransMode_Ethernet";
            this.RBtn_TransMode_Ethernet.Size = new System.Drawing.Size(65, 17);
            this.RBtn_TransMode_Ethernet.TabIndex = 1;
            this.RBtn_TransMode_Ethernet.Text = "Ethernet";
            this.RBtn_TransMode_Ethernet.UseVisualStyleBackColor = true;
            this.RBtn_TransMode_Ethernet.CheckedChanged += new System.EventHandler(this.TransMode_CheckedChanged);
            // 
            // RBtn_TransMode_RS232
            // 
            this.RBtn_TransMode_RS232.AutoSize = true;
            this.RBtn_TransMode_RS232.Checked = true;
            this.RBtn_TransMode_RS232.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.RBtn_TransMode_RS232.Location = new System.Drawing.Point(3, 18);
            this.RBtn_TransMode_RS232.Name = "RBtn_TransMode_RS232";
            this.RBtn_TransMode_RS232.Size = new System.Drawing.Size(61, 17);
            this.RBtn_TransMode_RS232.TabIndex = 0;
            this.RBtn_TransMode_RS232.TabStop = true;
            this.RBtn_TransMode_RS232.Text = "RS-232";
            this.RBtn_TransMode_RS232.UseVisualStyleBackColor = true;
            // 
            // GroupBox_ComportSetting
            // 
            this.GroupBox_ComportSetting.Controls.Add(this.Btn_COMRefresh);
            this.GroupBox_ComportSetting.Controls.Add(this.ProgressBar_ComportState);
            this.GroupBox_ComportSetting.Controls.Add(this.Btn_COMCLOSE);
            this.GroupBox_ComportSetting.Controls.Add(this.Btn_COMOPEN);
            this.GroupBox_ComportSetting.Controls.Add(this.Chk_CD);
            this.GroupBox_ComportSetting.Controls.Add(this.Chk_DSR);
            this.GroupBox_ComportSetting.Controls.Add(this.Chk_CTS);
            this.GroupBox_ComportSetting.Controls.Add(this.Chk_RTS);
            this.GroupBox_ComportSetting.Controls.Add(this.Chk_DTR);
            this.GroupBox_ComportSetting.Controls.Add(this.Lb_ParityBits);
            this.GroupBox_ComportSetting.Controls.Add(this.Lb_StopBits);
            this.GroupBox_ComportSetting.Controls.Add(this.CBoxParityBits);
            this.GroupBox_ComportSetting.Controls.Add(this.CBoxStopBits);
            this.GroupBox_ComportSetting.Controls.Add(this.CBoxDataBits);
            this.GroupBox_ComportSetting.Controls.Add(this.Lb_DataBits);
            this.GroupBox_ComportSetting.Controls.Add(this.CBoxBaudRate);
            this.GroupBox_ComportSetting.Controls.Add(this.Lb_BaudRate);
            this.GroupBox_ComportSetting.Controls.Add(this.CBoxCOMPORT);
            this.GroupBox_ComportSetting.Controls.Add(this.Lb_ComPort);
            this.GroupBox_ComportSetting.Font = new System.Drawing.Font("Gulim", 9F, ((System.Drawing.FontStyle)((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Underline))), System.Drawing.GraphicsUnit.Point, ((byte)(129)));
            this.GroupBox_ComportSetting.Location = new System.Drawing.Point(12, 61);
            this.GroupBox_ComportSetting.Name = "GroupBox_ComportSetting";
            this.GroupBox_ComportSetting.Size = new System.Drawing.Size(153, 220);
            this.GroupBox_ComportSetting.TabIndex = 3;
            this.GroupBox_ComportSetting.TabStop = false;
            this.GroupBox_ComportSetting.Text = "Com Port Setting";
            // 
            // Btn_COMRefresh
            // 
            this.Btn_COMRefresh.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_COMRefresh.Location = new System.Drawing.Point(99, 183);
            this.Btn_COMRefresh.Name = "Btn_COMRefresh";
            this.Btn_COMRefresh.Size = new System.Drawing.Size(47, 23);
            this.Btn_COMRefresh.TabIndex = 72;
            this.Btn_COMRefresh.Text = "Refresh";
            this.Btn_COMRefresh.UseVisualStyleBackColor = true;
            // 
            // ProgressBar_ComportState
            // 
            this.ProgressBar_ComportState.Location = new System.Drawing.Point(5, 209);
            this.ProgressBar_ComportState.Name = "ProgressBar_ComportState";
            this.ProgressBar_ComportState.Size = new System.Drawing.Size(141, 5);
            this.ProgressBar_ComportState.TabIndex = 55;
            // 
            // Btn_COMCLOSE
            // 
            this.Btn_COMCLOSE.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_COMCLOSE.Location = new System.Drawing.Point(52, 183);
            this.Btn_COMCLOSE.Name = "Btn_COMCLOSE";
            this.Btn_COMCLOSE.Size = new System.Drawing.Size(47, 23);
            this.Btn_COMCLOSE.TabIndex = 71;
            this.Btn_COMCLOSE.Text = "CLOSE";
            this.Btn_COMCLOSE.UseVisualStyleBackColor = true;
            // 
            // Btn_COMOPEN
            // 
            this.Btn_COMOPEN.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_COMOPEN.Location = new System.Drawing.Point(5, 183);
            this.Btn_COMOPEN.Name = "Btn_COMOPEN";
            this.Btn_COMOPEN.Size = new System.Drawing.Size(47, 23);
            this.Btn_COMOPEN.TabIndex = 56;
            this.Btn_COMOPEN.Text = "OPEN";
            this.Btn_COMOPEN.UseVisualStyleBackColor = true;
            // 
            // Chk_CD
            // 
            this.Chk_CD.AutoSize = true;
            this.Chk_CD.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Chk_CD.Location = new System.Drawing.Point(57, 164);
            this.Chk_CD.Name = "Chk_CD";
            this.Chk_CD.Size = new System.Drawing.Size(41, 17);
            this.Chk_CD.TabIndex = 70;
            this.Chk_CD.Text = "CD";
            this.Chk_CD.UseVisualStyleBackColor = true;
            // 
            // Chk_DSR
            // 
            this.Chk_DSR.AutoSize = true;
            this.Chk_DSR.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Chk_DSR.Location = new System.Drawing.Point(9, 164);
            this.Chk_DSR.Name = "Chk_DSR";
            this.Chk_DSR.Size = new System.Drawing.Size(49, 17);
            this.Chk_DSR.TabIndex = 69;
            this.Chk_DSR.Text = "DSR";
            this.Chk_DSR.UseVisualStyleBackColor = true;
            // 
            // Chk_CTS
            // 
            this.Chk_CTS.AutoSize = true;
            this.Chk_CTS.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Chk_CTS.Location = new System.Drawing.Point(105, 146);
            this.Chk_CTS.Name = "Chk_CTS";
            this.Chk_CTS.Size = new System.Drawing.Size(47, 17);
            this.Chk_CTS.TabIndex = 68;
            this.Chk_CTS.Text = "CTS";
            this.Chk_CTS.UseVisualStyleBackColor = true;
            // 
            // Chk_RTS
            // 
            this.Chk_RTS.AutoSize = true;
            this.Chk_RTS.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Chk_RTS.Location = new System.Drawing.Point(57, 146);
            this.Chk_RTS.Name = "Chk_RTS";
            this.Chk_RTS.Size = new System.Drawing.Size(48, 17);
            this.Chk_RTS.TabIndex = 67;
            this.Chk_RTS.Text = "RTS";
            this.Chk_RTS.UseVisualStyleBackColor = true;
            // 
            // Chk_DTR
            // 
            this.Chk_DTR.AutoSize = true;
            this.Chk_DTR.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Chk_DTR.Location = new System.Drawing.Point(9, 146);
            this.Chk_DTR.Name = "Chk_DTR";
            this.Chk_DTR.Size = new System.Drawing.Size(49, 17);
            this.Chk_DTR.TabIndex = 57;
            this.Chk_DTR.Text = "DTR";
            this.Chk_DTR.UseVisualStyleBackColor = true;
            // 
            // Lb_ParityBits
            // 
            this.Lb_ParityBits.AutoSize = true;
            this.Lb_ParityBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Lb_ParityBits.Location = new System.Drawing.Point(5, 128);
            this.Lb_ParityBits.Name = "Lb_ParityBits";
            this.Lb_ParityBits.Size = new System.Drawing.Size(53, 13);
            this.Lb_ParityBits.TabIndex = 66;
            this.Lb_ParityBits.Text = "Parity Bits";
            // 
            // Lb_StopBits
            // 
            this.Lb_StopBits.AutoSize = true;
            this.Lb_StopBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Lb_StopBits.Location = new System.Drawing.Point(9, 101);
            this.Lb_StopBits.Name = "Lb_StopBits";
            this.Lb_StopBits.Size = new System.Drawing.Size(49, 13);
            this.Lb_StopBits.TabIndex = 65;
            this.Lb_StopBits.Text = "Stop Bits";
            // 
            // CBoxParityBits
            // 
            this.CBoxParityBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.CBoxParityBits.FormattingEnabled = true;
            this.CBoxParityBits.Items.AddRange(new object[] {
            "None",
            "Even",
            "Odd"});
            this.CBoxParityBits.Location = new System.Drawing.Point(64, 121);
            this.CBoxParityBits.Name = "CBoxParityBits";
            this.CBoxParityBits.Size = new System.Drawing.Size(83, 21);
            this.CBoxParityBits.TabIndex = 64;
            this.CBoxParityBits.Text = "None";
            // 
            // CBoxStopBits
            // 
            this.CBoxStopBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.CBoxStopBits.FormattingEnabled = true;
            this.CBoxStopBits.Items.AddRange(new object[] {
            "1",
            "2",
            "1.5"});
            this.CBoxStopBits.Location = new System.Drawing.Point(64, 95);
            this.CBoxStopBits.Name = "CBoxStopBits";
            this.CBoxStopBits.Size = new System.Drawing.Size(83, 21);
            this.CBoxStopBits.TabIndex = 63;
            this.CBoxStopBits.Text = "1";
            // 
            // CBoxDataBits
            // 
            this.CBoxDataBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.CBoxDataBits.FormattingEnabled = true;
            this.CBoxDataBits.Items.AddRange(new object[] {
            "8",
            "9"});
            this.CBoxDataBits.Location = new System.Drawing.Point(64, 69);
            this.CBoxDataBits.Name = "CBoxDataBits";
            this.CBoxDataBits.Size = new System.Drawing.Size(83, 21);
            this.CBoxDataBits.TabIndex = 62;
            this.CBoxDataBits.Text = "8";
            // 
            // Lb_DataBits
            // 
            this.Lb_DataBits.AutoSize = true;
            this.Lb_DataBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Lb_DataBits.Location = new System.Drawing.Point(8, 74);
            this.Lb_DataBits.Name = "Lb_DataBits";
            this.Lb_DataBits.Size = new System.Drawing.Size(50, 13);
            this.Lb_DataBits.TabIndex = 61;
            this.Lb_DataBits.Text = "Data Bits";
            // 
            // CBoxBaudRate
            // 
            this.CBoxBaudRate.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.CBoxBaudRate.FormattingEnabled = true;
            this.CBoxBaudRate.Items.AddRange(new object[] {
            "300",
            "1200",
            "2400",
            "4800",
            "9600",
            "19200",
            "38400",
            "57600",
            "74880",
            "115200",
            "230400",
            "250000",
            "500000",
            "921600",
            "1000000",
            "3000000"});
            this.CBoxBaudRate.Location = new System.Drawing.Point(64, 43);
            this.CBoxBaudRate.Name = "CBoxBaudRate";
            this.CBoxBaudRate.Size = new System.Drawing.Size(83, 21);
            this.CBoxBaudRate.TabIndex = 60;
            this.CBoxBaudRate.Text = "921600";
            // 
            // Lb_BaudRate
            // 
            this.Lb_BaudRate.AutoSize = true;
            this.Lb_BaudRate.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Lb_BaudRate.Location = new System.Drawing.Point(1, 47);
            this.Lb_BaudRate.Name = "Lb_BaudRate";
            this.Lb_BaudRate.Size = new System.Drawing.Size(58, 13);
            this.Lb_BaudRate.TabIndex = 59;
            this.Lb_BaudRate.Text = "Baud Rate";
            // 
            // CBoxCOMPORT
            // 
            this.CBoxCOMPORT.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.CBoxCOMPORT.FormattingEnabled = true;
            this.CBoxCOMPORT.Location = new System.Drawing.Point(64, 17);
            this.CBoxCOMPORT.Name = "CBoxCOMPORT";
            this.CBoxCOMPORT.Size = new System.Drawing.Size(83, 21);
            this.CBoxCOMPORT.TabIndex = 58;
            // 
            // Lb_ComPort
            // 
            this.Lb_ComPort.AutoSize = true;
            this.Lb_ComPort.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Lb_ComPort.Location = new System.Drawing.Point(5, 22);
            this.Lb_ComPort.Name = "Lb_ComPort";
            this.Lb_ComPort.Size = new System.Drawing.Size(53, 13);
            this.Lb_ComPort.TabIndex = 54;
            this.Lb_ComPort.Text = "COM Port";
            // 
            // GroupBox_SocketSetting
            // 
            this.GroupBox_SocketSetting.Controls.Add(this.ProgressBar_SocketState);
            this.GroupBox_SocketSetting.Controls.Add(this.Btn_SOCKETDisconnect);
            this.GroupBox_SocketSetting.Controls.Add(this.Tbox_Port);
            this.GroupBox_SocketSetting.Controls.Add(this.label7);
            this.GroupBox_SocketSetting.Controls.Add(this.Btn_SOCKETConnect);
            this.GroupBox_SocketSetting.Controls.Add(this.Tbox_IPAddress);
            this.GroupBox_SocketSetting.Controls.Add(this.label11);
            this.GroupBox_SocketSetting.Enabled = false;
            this.GroupBox_SocketSetting.Font = new System.Drawing.Font("Gulim", 9F, ((System.Drawing.FontStyle)((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Underline))), System.Drawing.GraphicsUnit.Point, ((byte)(129)));
            this.GroupBox_SocketSetting.Location = new System.Drawing.Point(12, 287);
            this.GroupBox_SocketSetting.Name = "GroupBox_SocketSetting";
            this.GroupBox_SocketSetting.Size = new System.Drawing.Size(153, 108);
            this.GroupBox_SocketSetting.TabIndex = 43;
            this.GroupBox_SocketSetting.TabStop = false;
            this.GroupBox_SocketSetting.Text = "Socket Setting";
            // 
            // ProgressBar_SocketState
            // 
            this.ProgressBar_SocketState.Location = new System.Drawing.Point(5, 96);
            this.ProgressBar_SocketState.Name = "ProgressBar_SocketState";
            this.ProgressBar_SocketState.Size = new System.Drawing.Size(143, 5);
            this.ProgressBar_SocketState.TabIndex = 73;
            // 
            // Btn_SOCKETDisconnect
            // 
            this.Btn_SOCKETDisconnect.Enabled = false;
            this.Btn_SOCKETDisconnect.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_SOCKETDisconnect.Location = new System.Drawing.Point(84, 73);
            this.Btn_SOCKETDisconnect.Name = "Btn_SOCKETDisconnect";
            this.Btn_SOCKETDisconnect.Size = new System.Drawing.Size(64, 23);
            this.Btn_SOCKETDisconnect.TabIndex = 10;
            this.Btn_SOCKETDisconnect.Text = "Disconnect";
            this.Btn_SOCKETDisconnect.UseVisualStyleBackColor = true;
            // 
            // Tbox_Port
            // 
            this.Tbox_Port.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Tbox_Port.Location = new System.Drawing.Point(58, 48);
            this.Tbox_Port.Name = "Tbox_Port";
            this.Tbox_Port.Size = new System.Drawing.Size(91, 20);
            this.Tbox_Port.TabIndex = 7;
            this.Tbox_Port.Text = "7";
            // 
            // label7
            // 
            this.label7.AutoSize = true;
            this.label7.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.label7.Location = new System.Drawing.Point(31, 52);
            this.label7.Name = "label7";
            this.label7.Size = new System.Drawing.Size(26, 13);
            this.label7.TabIndex = 8;
            this.label7.Text = "Port";
            // 
            // Btn_SOCKETConnect
            // 
            this.Btn_SOCKETConnect.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_SOCKETConnect.Location = new System.Drawing.Point(5, 73);
            this.Btn_SOCKETConnect.Name = "Btn_SOCKETConnect";
            this.Btn_SOCKETConnect.Size = new System.Drawing.Size(64, 22);
            this.Btn_SOCKETConnect.TabIndex = 9;
            this.Btn_SOCKETConnect.Text = "Connect";
            this.Btn_SOCKETConnect.UseVisualStyleBackColor = true;
            // 
            // Tbox_IPAddress
            // 
            this.Tbox_IPAddress.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Tbox_IPAddress.Location = new System.Drawing.Point(58, 24);
            this.Tbox_IPAddress.Name = "Tbox_IPAddress";
            this.Tbox_IPAddress.Size = new System.Drawing.Size(91, 20);
            this.Tbox_IPAddress.TabIndex = 6;
            this.Tbox_IPAddress.Text = "192.168.172.128";
            // 
            // label11
            // 
            this.label11.AutoSize = true;
            this.label11.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.label11.Location = new System.Drawing.Point(3, 28);
            this.label11.Name = "label11";
            this.label11.Size = new System.Drawing.Size(58, 13);
            this.label11.TabIndex = 5;
            this.label11.Text = "IP Address";
            // 
            // StatusStrip1
            // 
            this.StatusStrip1.AutoSize = false;
            this.StatusStrip1.ImageScalingSize = new System.Drawing.Size(20, 20);
            this.StatusStrip1.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.TStripProBar_SendState,
            this.TSStatusLb_Percent,
            this.TSStatusLb_nCounter});
            this.StatusStrip1.Location = new System.Drawing.Point(0, 404);
            this.StatusStrip1.Name = "StatusStrip1";
            this.StatusStrip1.Padding = new System.Windows.Forms.Padding(1, 0, 12, 0);
            this.StatusStrip1.RenderMode = System.Windows.Forms.ToolStripRenderMode.ManagerRenderMode;
            this.StatusStrip1.Size = new System.Drawing.Size(800, 23);
            this.StatusStrip1.TabIndex = 44;
            this.StatusStrip1.Text = "statusStrip1";
            // 
            // TStripProBar_SendState
            // 
            this.TStripProBar_SendState.AutoSize = false;
            this.TStripProBar_SendState.Name = "TStripProBar_SendState";
            this.TStripProBar_SendState.Size = new System.Drawing.Size(100, 17);
            // 
            // TSStatusLb_Percent
            // 
            this.TSStatusLb_Percent.Name = "TSStatusLb_Percent";
            this.TSStatusLb_Percent.Size = new System.Drawing.Size(0, 18);
            // 
            // TSStatusLb_nCounter
            // 
            this.TSStatusLb_nCounter.Name = "TSStatusLb_nCounter";
            this.TSStatusLb_nCounter.Size = new System.Drawing.Size(0, 18);
            // 
            // GroupBox_ViewerRS232
            // 
            this.GroupBox_ViewerRS232.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_ViewerRS232.Controls.Add(this.RadBtn_ASC);
            this.GroupBox_ViewerRS232.Controls.Add(this.RadBtn_Byte);
            this.GroupBox_ViewerRS232.Controls.Add(this.Btn_Clear_RS232Log);
            this.GroupBox_ViewerRS232.Controls.Add(this.Tbox_RS232_ReceivedLog);
            this.GroupBox_ViewerRS232.Font = new System.Drawing.Font("Gulim", 9F, ((System.Drawing.FontStyle)((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Underline))));
            this.GroupBox_ViewerRS232.Location = new System.Drawing.Point(172, 166);
            this.GroupBox_ViewerRS232.Name = "GroupBox_ViewerRS232";
            this.GroupBox_ViewerRS232.Size = new System.Drawing.Size(617, 229);
            this.GroupBox_ViewerRS232.TabIndex = 46;
            this.GroupBox_ViewerRS232.TabStop = false;
            this.GroupBox_ViewerRS232.Text = "Received Viewer(RS-232)";
            // 
            // RadBtn_ASC
            // 
            this.RadBtn_ASC.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.RadBtn_ASC.AutoSize = true;
            this.RadBtn_ASC.Checked = true;
            this.RadBtn_ASC.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.RadBtn_ASC.Location = new System.Drawing.Point(48, 20);
            this.RadBtn_ASC.Name = "RadBtn_ASC";
            this.RadBtn_ASC.Size = new System.Drawing.Size(52, 17);
            this.RadBtn_ASC.TabIndex = 33;
            this.RadBtn_ASC.TabStop = true;
            this.RadBtn_ASC.Text = "ASCⅡ";
            this.RadBtn_ASC.UseVisualStyleBackColor = true;
            // 
            // RadBtn_Byte
            // 
            this.RadBtn_Byte.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.RadBtn_Byte.AutoSize = true;
            this.RadBtn_Byte.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.RadBtn_Byte.Location = new System.Drawing.Point(5, 20);
            this.RadBtn_Byte.Name = "RadBtn_Byte";
            this.RadBtn_Byte.Size = new System.Drawing.Size(46, 17);
            this.RadBtn_Byte.TabIndex = 32;
            this.RadBtn_Byte.Text = "Byte";
            this.RadBtn_Byte.UseVisualStyleBackColor = true;
            // 
            // Btn_Clear_RS232Log
            // 
            this.Btn_Clear_RS232Log.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.Btn_Clear_RS232Log.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_Clear_RS232Log.Location = new System.Drawing.Point(550, 12);
            this.Btn_Clear_RS232Log.Name = "Btn_Clear_RS232Log";
            this.Btn_Clear_RS232Log.Size = new System.Drawing.Size(63, 25);
            this.Btn_Clear_RS232Log.TabIndex = 31;
            this.Btn_Clear_RS232Log.Text = "Clear";
            this.Btn_Clear_RS232Log.UseVisualStyleBackColor = true;
            // 
            // Tbox_RS232_ReceivedLog
            // 
            this.Tbox_RS232_ReceivedLog.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.Tbox_RS232_ReceivedLog.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Tbox_RS232_ReceivedLog.Location = new System.Drawing.Point(5, 48);
            this.Tbox_RS232_ReceivedLog.Multiline = true;
            this.Tbox_RS232_ReceivedLog.Name = "Tbox_RS232_ReceivedLog";
            this.Tbox_RS232_ReceivedLog.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this.Tbox_RS232_ReceivedLog.Size = new System.Drawing.Size(609, 176);
            this.Tbox_RS232_ReceivedLog.TabIndex = 27;
            // 
            // GroupBox_ViewerEthernet
            // 
            this.GroupBox_ViewerEthernet.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.GroupBox_ViewerEthernet.Controls.Add(this.RadBtn_ASC_Eth);
            this.GroupBox_ViewerEthernet.Controls.Add(this.RadBtn_Byte_Eth);
            this.GroupBox_ViewerEthernet.Controls.Add(this.Btn_Clear_EthernetLog);
            this.GroupBox_ViewerEthernet.Controls.Add(this.Tbox_Ethernet_ReceivedLog);
            this.GroupBox_ViewerEthernet.Font = new System.Drawing.Font("Gulim", 9F, ((System.Drawing.FontStyle)((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Underline))));
            this.GroupBox_ViewerEthernet.Location = new System.Drawing.Point(172, 166);
            this.GroupBox_ViewerEthernet.Name = "GroupBox_ViewerEthernet";
            this.GroupBox_ViewerEthernet.Size = new System.Drawing.Size(617, 229);
            this.GroupBox_ViewerEthernet.TabIndex = 47;
            this.GroupBox_ViewerEthernet.TabStop = false;
            this.GroupBox_ViewerEthernet.Text = "Received Viewer(Ethernet)";
            this.GroupBox_ViewerEthernet.Visible = false;
            // 
            // RadBtn_ASC_Eth
            // 
            this.RadBtn_ASC_Eth.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.RadBtn_ASC_Eth.AutoSize = true;
            this.RadBtn_ASC_Eth.Checked = true;
            this.RadBtn_ASC_Eth.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.RadBtn_ASC_Eth.Location = new System.Drawing.Point(48, 20);
            this.RadBtn_ASC_Eth.Name = "RadBtn_ASC_Eth";
            this.RadBtn_ASC_Eth.Size = new System.Drawing.Size(52, 17);
            this.RadBtn_ASC_Eth.TabIndex = 33;
            this.RadBtn_ASC_Eth.TabStop = true;
            this.RadBtn_ASC_Eth.Text = "ASCⅡ";
            this.RadBtn_ASC_Eth.UseVisualStyleBackColor = true;
            // 
            // RadBtn_Byte_Eth
            // 
            this.RadBtn_Byte_Eth.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.RadBtn_Byte_Eth.AutoSize = true;
            this.RadBtn_Byte_Eth.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.RadBtn_Byte_Eth.Location = new System.Drawing.Point(5, 20);
            this.RadBtn_Byte_Eth.Name = "RadBtn_Byte_Eth";
            this.RadBtn_Byte_Eth.Size = new System.Drawing.Size(46, 17);
            this.RadBtn_Byte_Eth.TabIndex = 32;
            this.RadBtn_Byte_Eth.Text = "Byte";
            this.RadBtn_Byte_Eth.UseVisualStyleBackColor = true;
            // 
            // Btn_Clear_EthernetLog
            // 
            this.Btn_Clear_EthernetLog.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.Btn_Clear_EthernetLog.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Btn_Clear_EthernetLog.Location = new System.Drawing.Point(550, 12);
            this.Btn_Clear_EthernetLog.Name = "Btn_Clear_EthernetLog";
            this.Btn_Clear_EthernetLog.Size = new System.Drawing.Size(63, 25);
            this.Btn_Clear_EthernetLog.TabIndex = 31;
            this.Btn_Clear_EthernetLog.Text = "Clear";
            this.Btn_Clear_EthernetLog.UseVisualStyleBackColor = true;
            // 
            // Tbox_Ethernet_ReceivedLog
            // 
            this.Tbox_Ethernet_ReceivedLog.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.Tbox_Ethernet_ReceivedLog.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.Tbox_Ethernet_ReceivedLog.Location = new System.Drawing.Point(5, 48);
            this.Tbox_Ethernet_ReceivedLog.Multiline = true;
            this.Tbox_Ethernet_ReceivedLog.Name = "Tbox_Ethernet_ReceivedLog";
            this.Tbox_Ethernet_ReceivedLog.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this.Tbox_Ethernet_ReceivedLog.Size = new System.Drawing.Size(609, 176);
            this.Tbox_Ethernet_ReceivedLog.TabIndex = 27;
            // 
            // Btn_FW_Open
            // 
            this.Btn_FW_Open.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.Btn_FW_Open.Location = new System.Drawing.Point(377, 72);
            this.Btn_FW_Open.Name = "Btn_FW_Open";
            this.Btn_FW_Open.Size = new System.Drawing.Size(75, 23);
            this.Btn_FW_Open.TabIndex = 48;
            this.Btn_FW_Open.Text = "open";
            this.Btn_FW_Open.UseVisualStyleBackColor = true;
            // 
            // textBox_FW_FIle
            // 
            this.textBox_FW_FIle.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textBox_FW_FIle.Location = new System.Drawing.Point(189, 73);
            this.textBox_FW_FIle.Name = "textBox_FW_FIle";
            this.textBox_FW_FIle.Size = new System.Drawing.Size(182, 21);
            this.textBox_FW_FIle.TabIndex = 49;
            // 
            // label_FW_Size
            // 
            this.label_FW_Size.AutoSize = true;
            this.label_FW_Size.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label_FW_Size.Location = new System.Drawing.Point(13, 100);
            this.label_FW_Size.Name = "label_FW_Size";
            this.label_FW_Size.Size = new System.Drawing.Size(36, 12);
            this.label_FW_Size.TabIndex = 50;
            this.label_FW_Size.Text = "0Byte";
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label2.Location = new System.Drawing.Point(12, 77);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(57, 13);
            this.label2.TabIndex = 51;
            this.label2.Text = "FW_APP";
            // 
            // groupBox_FWUpdate
            // 
            this.groupBox_FWUpdate.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.groupBox_FWUpdate.Controls.Add(this.Btn_ToFACTORY);
            this.groupBox_FWUpdate.Controls.Add(this.textBox_APP_Address);
            this.groupBox_FWUpdate.Controls.Add(this.textBox_FACT_Address);
            this.groupBox_FWUpdate.Controls.Add(this.Btn_FACTORY_Download);
            this.groupBox_FWUpdate.Controls.Add(this.label1);
            this.groupBox_FWUpdate.Controls.Add(this.label3);
            this.groupBox_FWUpdate.Controls.Add(this.textBox_FACTORY_FIle);
            this.groupBox_FWUpdate.Controls.Add(this.Btn_FACTORY_Open);
            this.groupBox_FWUpdate.Controls.Add(this.Btn_FW_Download);
            this.groupBox_FWUpdate.Controls.Add(this.label2);
            this.groupBox_FWUpdate.Controls.Add(this.label_FW_Size);
            this.groupBox_FWUpdate.Controls.Add(this.textBox_FW_FIle);
            this.groupBox_FWUpdate.Controls.Add(this.Btn_FW_Open);
            this.groupBox_FWUpdate.Font = new System.Drawing.Font("Gulim", 9F, ((System.Drawing.FontStyle)((System.Drawing.FontStyle.Bold | System.Drawing.FontStyle.Underline))));
            this.groupBox_FWUpdate.Location = new System.Drawing.Point(172, 12);
            this.groupBox_FWUpdate.Name = "groupBox_FWUpdate";
            this.groupBox_FWUpdate.Size = new System.Drawing.Size(617, 148);
            this.groupBox_FWUpdate.TabIndex = 48;
            this.groupBox_FWUpdate.TabStop = false;
            this.groupBox_FWUpdate.Text = "Firmware Update";
            // 
            // Btn_FW_Download
            // 
            this.Btn_FW_Download.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.Btn_FW_Download.Location = new System.Drawing.Point(458, 72);
            this.Btn_FW_Download.Name = "Btn_FW_Download";
            this.Btn_FW_Download.Size = new System.Drawing.Size(144, 23);
            this.Btn_FW_Download.TabIndex = 52;
            this.Btn_FW_Download.Text = "F/W Download";
            this.Btn_FW_Download.UseVisualStyleBackColor = true;
            // 
            // Btn_FACTORY_Download
            // 
            this.Btn_FACTORY_Download.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.Btn_FACTORY_Download.Location = new System.Drawing.Point(458, 18);
            this.Btn_FACTORY_Download.Name = "Btn_FACTORY_Download";
            this.Btn_FACTORY_Download.Size = new System.Drawing.Size(144, 23);
            this.Btn_FACTORY_Download.TabIndex = 57;
            this.Btn_FACTORY_Download.Text = "FACTORY Download";
            this.Btn_FACTORY_Download.UseVisualStyleBackColor = true;
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label1.Location = new System.Drawing.Point(12, 23);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(64, 13);
            this.label1.TabIndex = 56;
            this.label1.Text = "FACTORY";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.label3.Location = new System.Drawing.Point(13, 46);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(36, 12);
            this.label3.TabIndex = 55;
            this.label3.Text = "0Byte";
            // 
            // textBox_FACTORY_FIle
            // 
            this.textBox_FACTORY_FIle.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textBox_FACTORY_FIle.Location = new System.Drawing.Point(189, 19);
            this.textBox_FACTORY_FIle.Name = "textBox_FACTORY_FIle";
            this.textBox_FACTORY_FIle.Size = new System.Drawing.Size(182, 21);
            this.textBox_FACTORY_FIle.TabIndex = 54;
            // 
            // Btn_FACTORY_Open
            // 
            this.Btn_FACTORY_Open.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.Btn_FACTORY_Open.Location = new System.Drawing.Point(377, 18);
            this.Btn_FACTORY_Open.Name = "Btn_FACTORY_Open";
            this.Btn_FACTORY_Open.Size = new System.Drawing.Size(75, 23);
            this.Btn_FACTORY_Open.TabIndex = 53;
            this.Btn_FACTORY_Open.Text = "open";
            this.Btn_FACTORY_Open.UseVisualStyleBackColor = true;
            // 
            // textBox_FACT_Address
            // 
            this.textBox_FACT_Address.Enabled = false;
            this.textBox_FACT_Address.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textBox_FACT_Address.Location = new System.Drawing.Point(82, 19);
            this.textBox_FACT_Address.Name = "textBox_FACT_Address";
            this.textBox_FACT_Address.Size = new System.Drawing.Size(101, 21);
            this.textBox_FACT_Address.TabIndex = 58;
            // 
            // textBox_APP_Address
            // 
            this.textBox_APP_Address.Enabled = false;
            this.textBox_APP_Address.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textBox_APP_Address.Location = new System.Drawing.Point(82, 73);
            this.textBox_APP_Address.Name = "textBox_APP_Address";
            this.textBox_APP_Address.Size = new System.Drawing.Size(101, 21);
            this.textBox_APP_Address.TabIndex = 59;
            // 
            // Btn_ToFACTORY
            // 
            this.Btn_ToFACTORY.Font = new System.Drawing.Font("Gulim", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.Btn_ToFACTORY.Location = new System.Drawing.Point(5, 116);
            this.Btn_ToFACTORY.Name = "Btn_ToFACTORY";
            this.Btn_ToFACTORY.Size = new System.Drawing.Size(300, 23);
            this.Btn_ToFACTORY.TabIndex = 60;
            this.Btn_ToFACTORY.Text = "FACTORY WORK";
            this.Btn_ToFACTORY.UseVisualStyleBackColor = true;
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(800, 427);
            this.Controls.Add(this.groupBox_FWUpdate);
            this.Controls.Add(this.StatusStrip1);
            this.Controls.Add(this.GroupBox_SocketSetting);
            this.Controls.Add(this.GroupBox_ComportSetting);
            this.Controls.Add(this.GroupBox_TransferMode);
            this.Controls.Add(this.GroupBox_ViewerEthernet);
            this.Controls.Add(this.GroupBox_ViewerRS232);
            this.Name = "Form1";
            this.Text = "Form1";
            this.GroupBox_TransferMode.ResumeLayout(false);
            this.GroupBox_TransferMode.PerformLayout();
            this.GroupBox_ComportSetting.ResumeLayout(false);
            this.GroupBox_ComportSetting.PerformLayout();
            this.GroupBox_SocketSetting.ResumeLayout(false);
            this.GroupBox_SocketSetting.PerformLayout();
            this.StatusStrip1.ResumeLayout(false);
            this.StatusStrip1.PerformLayout();
            this.GroupBox_ViewerRS232.ResumeLayout(false);
            this.GroupBox_ViewerRS232.PerformLayout();
            this.GroupBox_ViewerEthernet.ResumeLayout(false);
            this.GroupBox_ViewerEthernet.PerformLayout();
            this.groupBox_FWUpdate.ResumeLayout(false);
            this.groupBox_FWUpdate.PerformLayout();
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.GroupBox GroupBox_TransferMode;
        private System.Windows.Forms.RadioButton RBtn_TransMode_Ethernet;
        private System.Windows.Forms.RadioButton RBtn_TransMode_RS232;
        private System.Windows.Forms.GroupBox GroupBox_ComportSetting;
        private System.Windows.Forms.Button Btn_COMRefresh;
        private System.Windows.Forms.ProgressBar ProgressBar_ComportState;
        private System.Windows.Forms.Button Btn_COMCLOSE;
        private System.Windows.Forms.Button Btn_COMOPEN;
        private System.Windows.Forms.CheckBox Chk_CD;
        private System.Windows.Forms.CheckBox Chk_DSR;
        private System.Windows.Forms.CheckBox Chk_CTS;
        private System.Windows.Forms.CheckBox Chk_RTS;
        private System.Windows.Forms.CheckBox Chk_DTR;
        private System.Windows.Forms.Label Lb_ParityBits;
        private System.Windows.Forms.Label Lb_StopBits;
        private System.Windows.Forms.ComboBox CBoxParityBits;
        private System.Windows.Forms.ComboBox CBoxStopBits;
        private System.Windows.Forms.ComboBox CBoxDataBits;
        private System.Windows.Forms.Label Lb_DataBits;
        private System.Windows.Forms.ComboBox CBoxBaudRate;
        private System.Windows.Forms.Label Lb_BaudRate;
        private System.Windows.Forms.ComboBox CBoxCOMPORT;
        private System.Windows.Forms.Label Lb_ComPort;
        private System.Windows.Forms.GroupBox GroupBox_SocketSetting;
        private System.Windows.Forms.ProgressBar ProgressBar_SocketState;
        private System.Windows.Forms.Button Btn_SOCKETDisconnect;
        private System.Windows.Forms.TextBox Tbox_Port;
        private System.Windows.Forms.Label label7;
        private System.Windows.Forms.Button Btn_SOCKETConnect;
        private System.Windows.Forms.TextBox Tbox_IPAddress;
        private System.Windows.Forms.Label label11;
        private System.Windows.Forms.StatusStrip StatusStrip1;
        private System.Windows.Forms.ToolStripProgressBar TStripProBar_SendState;
        private System.Windows.Forms.ToolStripStatusLabel TSStatusLb_Percent;
        private System.Windows.Forms.ToolStripStatusLabel TSStatusLb_nCounter;
        private System.Windows.Forms.GroupBox GroupBox_ViewerRS232;
        private System.Windows.Forms.RadioButton RadBtn_ASC;
        private System.Windows.Forms.RadioButton RadBtn_Byte;
        private System.Windows.Forms.Button Btn_Clear_RS232Log;
        private System.Windows.Forms.TextBox Tbox_RS232_ReceivedLog;
        private System.Windows.Forms.GroupBox GroupBox_ViewerEthernet;
        private System.Windows.Forms.RadioButton RadBtn_ASC_Eth;
        private System.Windows.Forms.RadioButton RadBtn_Byte_Eth;
        private System.Windows.Forms.Button Btn_Clear_EthernetLog;
        private System.Windows.Forms.TextBox Tbox_Ethernet_ReceivedLog;
        private System.Windows.Forms.Button Btn_FW_Open;
        private System.Windows.Forms.TextBox textBox_FW_FIle;
        private System.Windows.Forms.Label label_FW_Size;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.GroupBox groupBox_FWUpdate;
        private System.Windows.Forms.Button Btn_FW_Download;
        private System.Windows.Forms.TextBox textBox_APP_Address;
        private System.Windows.Forms.TextBox textBox_FACT_Address;
        private System.Windows.Forms.Button Btn_FACTORY_Download;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.TextBox textBox_FACTORY_FIle;
        private System.Windows.Forms.Button Btn_FACTORY_Open;
        private System.Windows.Forms.Button Btn_ToFACTORY;
    }
}

