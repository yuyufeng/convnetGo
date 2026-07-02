package main

import (
	"bufio"
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/md5"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"sync"
	"sync/atomic"

	"github.com/labstack/gommon/log"
)

// 新增全局变量
var (
	userIDCounter uint64                           // 自增用户ID计数器
	uuidToUserID  sync.Map                         // 存储UUID和用户ID的映射关系
	uuidFilePath  string   = "uuid_to_userid.json" // 存储路径
)

var serverConnMap sync.Map

type peerClient struct {
	Conn           *net.Conn
	PublicID       string
	MainClientInfo *mainClientInfo
}

// 新增函数：从文件中加载UUID到UserID的映射关系
func loadUUIDToUserID() error {
	data, err := os.ReadFile(uuidFilePath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil // 文件不存在时返回nil
		}
		return err
	}

	var uuidMap map[string]uint64
	if err := json.Unmarshal(data, &uuidMap); err != nil {
		return err
	}

	for uuid, userID := range uuidMap {
		uuidToUserID.Store(uuid, userID)
		if userID > userIDCounter {
			userIDCounter = userID // 更新userIDCounter
		}
	}
	return nil
}

// 新增函数：将UUID到UserID的映射关系保存到文件
func saveUUIDToUserID() error {
	uuidMap := make(map[string]uint64)
	uuidToUserID.Range(func(key, value interface{}) bool {
		uuidMap[key.(string)] = value.(uint64)
		return true
	})

	data, err := json.Marshal(uuidMap)
	if err != nil {
		return err
	}

	return os.WriteFile(uuidFilePath, data, 0644)
}

func SHServerInit() {
	// 加载已有的UUID到UserID映射关系
	if err := loadUUIDToUserID(); err != nil {
		fmt.Println("Error loading UUID to UserID mapping:", err)
		return
	}

	// 1. 开始监听指定端口
	listener, err := net.Listen("tcp", "0.0.0.0:13903")
	if err != nil {
		fmt.Println("Error starting TCP server:", err)
		return
	}
	defer listener.Close()
	fmt.Println("TCP server is listening on 0.0.0.0:13903")

	// 2. 持续接受客户端连接
	for {
		conn, err := listener.Accept() // 接收客户端连接
		if err != nil {
			fmt.Println("Error accepting connection:", err)
			continue
		}
		var peerClient peerClient
		peerClient.Conn = &conn

		// 3. 处理每个客户端连接
		go handleServerConnection(peerClient) // 使用 goroutine 并发处理
	}
}

type clientMessage struct {
	Version string
	CMDType int //1 online ,2 shakehand
	Message []interface{}
}

type PortRange struct {
	Start float64 // 起始端口
	End   float64 // 结束端口
}
type mainClientInfo struct {
	CanNat            bool        //是否可以NAT
	Name              string      //名称
	AllowTcpPortRange []PortRange //允许的TCP端口范围
	AllowUdpPortRange []PortRange //允许的UDP端口范围
	PublicID          string      //发布ID-唯一，对应于UUID的验证ID
	ClientMode        float64
	Mac               string
	SDP               string
	IP                string
	Version           string
}

type serverConn struct {
	Conn           *net.Conn
	MainClientInfo *mainClientInfo
}

// NewPortRange 用于创建一个新的端口范围
func NewPortRange(start, end float64) (*PortRange, error) {
	// 检查端口是否有效 (0-65535)
	if start < 0 || start > 65535 || end < 0 || end > 65535 || start > end {
		return nil, fmt.Errorf("invalid port range: %d-%d", (int)(start), (int)(end))
	}
	return &PortRange{Start: start, End: end}, nil
}

// Contains 用于判断某个端口是否在范围内
func (pr *PortRange) Contains(port float64) bool {
	return port >= pr.Start && port <= pr.End
}

// String 返回端口范围的字符串表示形式
func (pr *PortRange) String() string {
	return fmt.Sprintf("%d-%d", (int)(pr.Start), (int)(pr.End))
}

// PKCS7Padding 实现PKCS7填充
func PKCS7Padding(data []byte, blockSize int) []byte {
	padding := blockSize - len(data)%blockSize
	padtext := bytes.Repeat([]byte{byte(padding)}, padding)
	return append(data, padtext...)
}

// PKCS7UnPadding 实现PKCS7去除填充
func PKCS7UnPadding(data []byte) []byte {
	length := len(data)
	unpadding := int(data[length-1])
	return data[:(length - unpadding)]
}

// AesEncryptCBC AES CBC模式加密
func AesEncryptCBC(plaintext, key []byte) (string, error) {
	// 创建AES加密块
	block, err := aes.NewCipher(key)
	if err != nil {
		return "", err
	}

	// 使用PKCS7填充
	blockSize := block.BlockSize()
	plaintext = PKCS7Padding(plaintext, blockSize)

	// 创建一个随机的初始化向量（IV）
	iv := make([]byte, blockSize)
	if _, err := io.ReadFull(rand.Reader, iv); err != nil {
		return "", err
	}

	// 使用CBC模式进行加密
	blockMode := cipher.NewCBCEncrypter(block, iv)
	ciphertext := make([]byte, len(plaintext))
	blockMode.CryptBlocks(ciphertext, plaintext)

	// 返回Base64编码的密文 (iv + ciphertext)
	return base64.StdEncoding.EncodeToString(append(iv, ciphertext...)), nil
}

// AesDecryptCBC AES CBC模式解密
func AesDecryptCBC(encrypted string, key []byte) ([]byte, error) {
	// 解码Base64编码的密文
	ciphertext, err := base64.StdEncoding.DecodeString(encrypted)
	if err != nil {
		return nil, err
	}

	// 创建AES加密块
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	blockSize := block.BlockSize()
	if len(ciphertext) < blockSize {
		return nil, fmt.Errorf("ciphertext too short")
	}

	// 分离IV和密文
	iv := ciphertext[:blockSize]
	ciphertext = ciphertext[blockSize:]

	// 使用CBC模式解密
	blockMode := cipher.NewCBCDecrypter(block, iv)
	plaintext := make([]byte, len(ciphertext))
	blockMode.CryptBlocks(plaintext, ciphertext)

	// 去除PKCS7填充
	plaintext = PKCS7UnPadding(plaintext)

	return plaintext, nil
}

func ToPortRange(ranges []interface{}) []PortRange {

	//fmt.Println(ranges)
	var res []PortRange
	for _, v := range ranges {
		v := v.(map[string]interface{})
		//获取V的start
		PortRange1 := PortRange{v["Start"].(float64), v["End"].(float64)}

		res = append(res, PortRange1)
		//	fmt.Println(res)
	}
	return res
}

// 处理客户端连接的函数
func handleServerConnection(cvClient peerClient) {
	cvClient.PublicID = ""
	conn := *cvClient.Conn
	defer func() {
		if r := recover(); r != nil {
			fmt.Println("Recovered from panic:", r)
		}

		conn.Close() // 处理完成后关闭连接
		serverConnMap.Delete(cvClient.PublicID)
		onClientDisconnected(cvClient.PublicID) // 通知网络对端下线
		log.Info("连接断开>", cvClient.PublicID, "<")
	}()

	// 读取客户端请求数据
	reader := bufio.NewReader(conn)
	for {
		//读取两个字节的长度信息
		lengthBytes := make([]byte, ConstSaveDataLength)
		_, err := io.ReadFull(reader, lengthBytes)
		if err != nil {
			fmt.Println("Error reading from client:", err)
			return
		}
		//读取长度的消息
		length := BytesToInt(lengthBytes)

		messagebuff := make([]byte, length)
		_, err = io.ReadFull(reader, messagebuff)
		if err != nil {
			fmt.Println("Error reading from client:", err)
			return
		}
		message := (string)(messagebuff)

		// // 4. 从客户端读取一行请求数据
		// message, err := reader.ReadString('\n')
		// if err != nil {
		// 	fmt.Println("Error reading from client:", err)
		// 	return
		// }

		// 5. 去除末尾换行符并打印收到的消息
		fmt.Println("Received from client:", message)

		if message != "" {
			//解析为clientmessage对象
			var clientMessage clientMessage

			err := json.Unmarshal([]byte(message), &clientMessage)
			if err != nil {
				fmt.Println("Error parsing JSON:", err)
				conn.Close()
				return
			}

			log.Info("clientMessage:", length, clientMessage)

			switch clientMessage.CMDType {
			case WS_REGISTE:
				{
					log.Info("登录请求", clientMessage.Message)

					clientMessage.CMDType = 2

					var mainClientInfo mainClientInfo
					mainClientInfo.CanNat = true

					mainClientInfo.Version = clientMessage.Message[0].(string)
					UUID := clientMessage.Message[1].(string)
					mainClientInfo.Name = clientMessage.Message[2].(string)
					mainClientInfo.ClientMode = clientMessage.Message[3].(float64)
					mainClientInfo.Mac = clientMessage.Message[4].(string)

					//clientMessage.Message[4]转换为[]PortRange
					mainClientInfo.AllowTcpPortRange = ToPortRange(clientMessage.Message[6].([]interface{}))
					mainClientInfo.AllowUdpPortRange = ToPortRange(clientMessage.Message[7].([]interface{}))

					log.Info("mainClientInfo", mainClientInfo)
					//使用AES加密UUID
					// 1. 生成 MD5 哈希值
					hash := md5.New()
					hash.Write([]byte(UUID))
					hashBytes := hash.Sum(nil)

					// 2. 使用 Hex 编码将二进制哈希值转换为固定的字符串输出
					hashHex := hex.EncodeToString(hashBytes)

					// 检查是否已有对应的userID
					if existingUserID, ok := uuidToUserID.Load(UUID); ok {
						log.Info(existingUserID)
						mainClientInfo.IP = GetCvnIPstring(int(existingUserID.(uint64)))
						mainClientInfo.PublicID = hashHex + ":" + Inttostr(int(existingUserID.(uint64)))
					} else {
						// 生成自增用户ID并记录UUID和用户ID的对应关系
						userID := atomic.AddUint64(&userIDCounter, 1)
						uuidToUserID.Store(UUID, userID)
						mainClientInfo.IP = GetCvnIPstring(int(userID))
						mainClientInfo.PublicID = hashHex + ":" + Inttostr(int(userID))
						// 保存映射关系到文件
						if err := saveUUIDToUserID(); err != nil {
							fmt.Println("Error saving UUID to UserID mapping:", err)
						}
					}

					if err != nil {
						fmt.Println("Encryption failed:", err)
						sendToConn(conn, WS_REGISTE_FAIL, []interface{}{"服务器内部出错"})
					}

					cvClient.PublicID = mainClientInfo.PublicID
					_, ok := serverConnMap.Load(mainClientInfo.PublicID)
					if ok {
						sendToConn(conn, WS_REGISTE_FAIL, []interface{}{"登记失败，客户端UUID已在线"})
						conn.Close()
						return
					}

					var newServerconn serverConn
					newServerconn.Conn = &conn
					newServerconn.MainClientInfo = &mainClientInfo

					cvClient.MainClientInfo = &mainClientInfo
					//使用publicid作为主键存储服务器信息
					serverConnMap.Store(mainClientInfo.PublicID, newServerconn)
					persistIdentityOnRegister(&cvClient) // 身份落库（必须在响应前）
					// 回传服务器登记的权威昵称：客户端据此显示/校准，也支持换设备后取回昵称。
					// （QQ 模型：userID 唯一，昵称可重复、可被搜索）
					regNick := mainClientInfo.Name
					if store != nil {
						if u, ok := store.GetUser(userIDFromPublicID(mainClientInfo.PublicID)); ok && u.Nick != "" {
							regNick = u.Nick
						}
					}
					sendToConn(conn, WS_REGISTE_RESP, []interface{}{mainClientInfo.PublicID, regNick, mainClientInfo.IP})
					onClientRegistered(&cvClient) // 广播上线、下发离线申请/消息
				}
			case C_GETWS_SERVER_INFO: //获取客户端的身份
				{
					log.Info("获取服务器信息请求", clientMessage.Message)
					PublicID := clientMessage.Message[0].(string)
					v, ok := serverConnMap.Load(PublicID)
					if ok {
						serverconn := v.(serverConn)
						log.Info("获取服务器信息请求serverconn", serverconn)
						sendToConn(conn, C_GETWS_SERVER_INFO_RESP, []interface{}{serverconn.MainClientInfo})
					}
				}
			case C_CONNTOWS_PEERCALL:
				{
					log.Debug("连接请求", clientMessage.Message)
					SDP := clientMessage.Message[1].(string)
					step := clientMessage.Message[2].(string)
					accesspass := ""
					if len(clientMessage.Message) > 3 {
						accesspass = clientMessage.Message[3].(string)
					}

					conn2, ok := serverConnMap.Load(clientMessage.Message[0].(string))

					//将peer信息和SDP发送给peer客户端,剩下的交给WEBRTC做了
					if ok {
						log.Info("Call")
						log.Info(conn2.(serverConn).MainClientInfo)
						log.Info("From")
						log.Info(cvClient.MainClientInfo)

						sendToConn(*conn2.(serverConn).Conn, C_CONNTOWS_PEERCALL_RESP,
							[]interface{}{cvClient.MainClientInfo, []string{SDP}, step, accesspass})
						//log.Info("conn2send")
						//sendToConn(*cvClient.Conn, C_CONNTOWS_PEERCALL_RESP,
						//	[]interface{}{*conn2.(serverConn).MainClientInfo, []string{SDP}})
						log.Info("conn2sendok")
					} else { //找不到客户端
						sendToConn(*cvClient.Conn, C_CONNTOWS_PEERCALL_RESP_NOTONLINE, clientMessage.Message)
					}

				}
			case ACCOUNT_REGISTER:
				handleAccountAuth(&cvClient, true, clientMessage.Message)
			case ACCOUNT_LOGIN:
				handleAccountAuth(&cvClient, false, clientMessage.Message)
			default:
				// IM 层 opcode（好友/群组/在线态/聊天/ACL/中继）
				handleIM(&cvClient, clientMessage.CMDType, clientMessage.Message)
			}

		}

	}
}
