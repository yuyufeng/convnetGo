package main

import (
	"fmt"
	"math/rand"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"time"

	"github.com/google/uuid"
	"github.com/labstack/gommon/log"
)

func getRandomWord() string {
	words := []string{"apple", "banana", "cherry", "dog", "elephant", "fish", "grape", "hamburger", "ice cream", "juice", "kiwi", "lemon", "mango", "noodle", "orange", "pear", "quinoa", "raspberry", "sandwich", "tomato", "unicorn", "vodka", "watermelon", "xylophone", "yogurt", "zebra"}

	word := words[rand.Intn(len(words))]
	return word
}

func keepLive() {
	for {
		//  检查是否已连接
		//log.Info("断线检查", client.IsConnected, client.RetryConnect)
		if !client.IsConnected && client.RetryConnect {
			log.Info("服务断开，正在重新连接服务器...")
			ConnectServer(client.Server, client.ServerPort)
		}
		time.Sleep(time.Second * 60)
	}
}

func main() {

	// 从 convnet.json 文件中读取并初始化 client 对象
	err := loadClientConfig("convnet.json")
	if err != nil {
		log.Fatalf("Failed to load client configuration: %v", err)
	}

	if len(os.Args) > 1 && os.Args[1] == "-s" { //服务端模式
		fmt.Println("Starting server...")
		// 使用实际的公网IP地址
		// 获取服务器的实际IP地址
		publicIP := "1.95.54.7" // 默认IP地址
		// 尝试解析域名获取IP
		ips, err := net.LookupIP(client.Server)
		if err == nil && len(ips) > 0 {
			// 使用第一个IPv4地址
			for _, ip := range ips {
				if ipv4 := ip.To4(); ipv4 != nil {
					publicIP = ipv4.String()
					break
				}
			}
			log.Info("已解析服务器域	名", client.Server, "的IP地址:", publicIP)
		} else {
			log.Warn("无法解析服务器域名", client.Server, "的IP地址，使用默认IP:", publicIP)
		}
		// 将 ServerTurnPort 字符串转换为整数

		turnPort, err := strconv.Atoi(client.ServerTurnPort)
		if err != nil {
			log.Fatalf("无法将 ServerTurnPort 转换为整数: %v", err)
		}

		startTurn(publicIP, turnPort, client.ServerTurnRealm, client.ServerTurnUser, client.ServerTurnPass)
		SHServerInit()
		client.ClientMode = SERVER
		return
	}

	//客户端模式开始

	if client.UUID == "" {
		//生成一個UUID
		client.UUID = uuid.New().String()
		saveClientConfig("convnet.json")
	}

	if client.ClientID == "" {
		//随机生成一个单词作为用户昵称
		client.ClientID = getRandomWord()
		saveClientConfig("convnet.json")
	}

	//client对象从convnet.json获取并且初始化
	log.Info("Hello ConvnetGo!")

	http.Handle("/", http.FileServer(http.Dir(".")))
	http.HandleFunc("/api/user/list", getUserList)
	http.HandleFunc("/api/info", getClientInfo)
	http.HandleFunc("/api/info/update", updateUserInfo)
	http.HandleFunc("/api/peer/connect", connectToPublicID)
	http.HandleFunc("/api/peer/removePublicId", removePublicId)
	http.HandleFunc("/api/client/connect", clientConnectToServer)
	http.HandleFunc("/api/client/disconnect", clientDisconnect)
	http.HandleFunc("/api/client/allowConnect", allowConnect)
	http.HandleFunc("/api/chat/send", sendChatMessageAPI)
	http.HandleFunc("/api/chat/history", getChatHistoryAPI)
	http.HandleFunc("/api/chat/clear", clearChatHistoryAPI)
	//CheckNat(client.UdpServerPort)

	go TapInit()
	go keepLive()

	for client.Mac == "" {
		log.Info("等待获取Mac地址...")
		time.Sleep(1 * time.Second)
	}

	ConnectServer(client.Server, client.ServerPort)

	//
	//StartHttpServer(8092, 30)
	var listenAddress string
	listenAddress = "127.0.0.1:8094"
	fmt.Printf("HTTP server listen on http://%s\n", listenAddress)
	//本地exec打开http://127.0.0.1:8092/
	exec.Command("cmd", "/c", "start", "http://"+listenAddress).Run()
	panic(http.ListenAndServe(listenAddress, nil))

}
