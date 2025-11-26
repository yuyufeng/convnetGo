package main

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
	"github.com/labstack/gommon/log"
	"github.com/pion/webrtc/v4"

	"github.com/songgao/water"
)

func GetCvnIP(userid int) net.IP {
	var ip = 0x0A6E0000
	userid++                       //和老版本适配
	offset := (userid / 254) * 2   //每255个地址中.0和.255无法使用
	ip = ip + int(userid) + offset //补位网络地址和广播地址
	data, _ := IntToBytes32(ip, 4) //换算为byte
	self := net.IP(data)           //转换成ip
	return self
}

func GetCvnIPstring(userid int) string {
	net := GetCvnIP(userid)
	//net.IP转string
	//netstr, _ := net.IPv4(byte(self[12]), byte(self[13]), byte(self[14]), byte(self[15]))

	return net.String()
}
func Setip() {
	var mask = net.IPv4Mask(255, 0, 0, 0)
	//隨機生成id，255*255*255的數內容的隨機id

	// 随机生成10000以内的正整数
	rand.Seed(time.Now().UnixNano()) // 初始化随机数生成器

	//string转int
	userid := Strtoint(strings.Split(client.PublicID, ":")[1])

	self := GetCvnIP(userid)
	log.Info("myCvnIP:", self)
	client.MyCvnIP = self.String()
	err := setupIfce(net.IPNet{IP: self, Mask: mask}, client.g_ifce.Name()) //网卡地址绑定
	if err != nil {
		log.Fatal(err)
	}

}

func handlePacket(packet []byte) {
	// 创建一个新的 Packet 对象
	p := gopacket.NewPacket(packet, layers.LayerTypeEthernet, gopacket.Default)
	//fmt.Print(">>>>>>>>>", len(packet))
	// 解析每一层协议
	// for _, layer := range p.Layers() {
	// 	fmt.Println("Layer:", layer.LayerType())
	// }

	tarmac := ""
	//获取以太网层
	if ethernetLayer := p.Layer(layers.LayerTypeEthernet); ethernetLayer != nil {
		ethernetPacket, _ := ethernetLayer.(*layers.Ethernet)
		//fmt.Printf("Src MAC: %s, Dst MAC: %s\n", ethernetPacket.SrcMAC, ethernetPacket.DstMAC)
		tarmac = ethernetPacket.DstMAC.String()
	}

	// 获取 IP 层
	if ipLayer := p.Layer(layers.LayerTypeIPv4); ipLayer != nil {
		//ipPacket, _ := ipLayer.(*layers.IPv4)
		//fmt.Printf("Src IP: %s, Dst IP: %s\n", ipPacket.SrcIP, ipPacket.DstIP)
	}

	// 获取 TCP 层
	if tcpLayer := p.Layer(layers.LayerTypeTCP); tcpLayer != nil {
		//tcpPacket, _ := tcpLayer.(*layers.TCP)
		//fmt.Printf("Src Port: %d, Dst Port: %d\n", tcpPacket.SrcPort, tcpPacket.DstPort)

	}
	// 获取 UDP 层
	if udpLayer := p.Layer(layers.LayerTypeUDP); udpLayer != nil {
		//udpLayer, _ := udpLayer.(*layers.UDP)
		//fmt.Printf("Src Port: %d, Dst Port: %d\n", udpLayer.SrcPort, udpLayer.DstPort)
	}

	if tarmac == "ff:ff:ff:ff:ff:ff" {
		//log.Info("广播包")
		//发送到所有客户端
		//遍历sync.Map

		connAddrMap.Range(func(key, value interface{}) bool {
			user := value.(*User) // 假设 value 是 *User 类型
			//fmt.Print("needsent", packet)
			if user.Dc == nil {
				log.Debug(user.PublicID, "dc通道不存在")
				return true
			}
			if user.Dc.ReadyState() != webrtc.DataChannelStateOpen {
				log.Debug(user.Dc.ReadyState())
				return true
			}

			if user != nil && user.Dc != nil && user.Dc.ReadyState() == webrtc.DataChannelStateOpen {
				user.Dc.Send(packet)
				user.Con_send = user.Con_send + int64(len(packet))
				log.Debug("bc >", len(packet))
			}
			return true // 继续遍历
		})
		return
	}

	//}

	user := GetUserByMac(tarmac)
	if user != nil {
		if user.Dc == nil {
			fmt.Print("dc通道不存在")
			return
		}
		if user.Dc.ReadyState() == webrtc.DataChannelStateOpen {
			user.Dc.Send(packet)
			user.Con_send = user.Con_send + int64(len(packet))
			log.Debug("st >", len(packet))
		}
	}

}

func readFile(filePath string) ([]string, error) {
	if _, err := os.Stat("autoConnectPeer.txt"); err == nil {
		// 文件存在
		file, err := os.Open("autoConnectPeer.txt")
		if err != nil {
			log.Error(err)
			return nil, err
		}
		defer file.Close()

		buf, err := io.ReadAll(file)
		if err != nil {
			log.Error(err)
			return nil, err
		}
		log.Info("autoConnectPeer.txt内容：", string(buf))

		return strings.Split(string(buf), "\n"), nil
	}
	return nil, nil
}

func LoadAutoConnectPeerList() {
	//读取：autoConnectPeer.txt
	any, err := readFile("autoConnectPeer.txt")
	if err != nil {
		log.Info("读取autoConnectPeer.txt失败", err)
	} else {
		for _, v := range any {
			log.Info("自动连接用户：", v)
			publicID := strings.Replace(v, "\r", "", -1)
			var user *User

			upperpbid := strings.ToUpper(publicID)
			if !strings.HasPrefix(upperpbid, strings.ToUpper("CVNID://"+client.Server+":"+client.ServerPort)) {
				log.Error("此CVNID和本服务器信息不匹配，不予连接")
				continue
			}

			publicID = strings.Split(publicID, "@")[1]

			user = GetUserByPublicID(publicID)
			if user == nil {
				user = new(User)
				user.PublicID = publicID
				if len(strings.Split(v, "@")) > 2 {
					user.AccessPass = strings.Split(v, "@")[2]
				}
				// clientinfo := formatjsontoMainClientInfo(clientMessage)
			}
			//TODO
			RenewUserList(user)
			go peerConnectionUpdate(user, "0") //更新SDP
			time.Sleep(time.Second * 2)
		}
	}
}

func TapLoopData() {

	dataCh, errCh := startRead(client.g_ifce) //启动网卡
	Setip()
	for { //塞入chain
		select {
		case buffer := <-dataCh:
			handlePacket(buffer)
			//fmt.Print(waterutil.MACDestination(buffer), ",")
			//fmt.Print("received frame:\n", buffer)
			continue
		case err := <-errCh:
			log.Info("TAP读取错误，请重启程序:", err)
			return
		}
	}
}
func TapInit() {

	if client.g_ifce == nil {
		ifce, err := water.New(water.Config{
			DeviceType: water.TAP,
		})
		if err != nil {
			log.Fatal(err)
		}
		client.g_ifce = ifce

		client.Mac = Getmymac(ifce.Name())
		log.Info("网卡名称:", ifce.Name())
		client.g_ifce = ifce
	}

}

const BUFFERSIZE = 1600

func startRead(ifce *water.Interface) (dataChan <-chan []byte, errChan <-chan error) {
	dataCh := make(chan []byte)
	errCh := make(chan error)
	go func() {
		for {
			//很奇怪，这里重新分配内存比固定一块内存所需要的消耗要小
			buffer := make([]byte, BUFFERSIZE)

			n, err := ifce.Read(buffer)
			if err != nil {
				errCh <- err
				break
			} else {
				buffer = buffer[:n:n]
				dataCh <- buffer
			}
		}
	}()
	return dataCh, errCh
}

func writePacket(packet []byte) {
	if _, err := client.g_ifce.Write(packet); err != nil {
		log.Info(err)
	}
}
func startPing(dst net.IP, _ bool) {
	if err := exec.Command("ping", "-n", "4", dst.String()).Start(); err != nil {
		log.Info(err)
	}
}

// 整形转换成字节
func IntToBytes32(n int, b byte) ([]byte, error) {
	switch b {
	case 1:
		tmp := int8(n)
		bytesBuffer := bytes.NewBuffer([]byte{})
		binary.Write(bytesBuffer, binary.BigEndian, &tmp)
		return bytesBuffer.Bytes(), nil
	case 2:
		tmp := int16(n)
		bytesBuffer := bytes.NewBuffer([]byte{})
		binary.Write(bytesBuffer, binary.BigEndian, &tmp)
		return bytesBuffer.Bytes(), nil
	case 3, 4:
		tmp := int32(n)
		bytesBuffer := bytes.NewBuffer([]byte{})
		binary.Write(bytesBuffer, binary.BigEndian, &tmp)
		return bytesBuffer.Bytes(), nil
	}
	return nil, fmt.Errorf("IntToBytes b param is invaild")
}

func String2Mac(str string) net.HardwareAddr {
	data, _ := hex.DecodeString(str)
	return data
}
