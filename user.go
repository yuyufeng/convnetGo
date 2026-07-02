package main

import (
	"net"
	"sync"

	"github.com/labstack/gommon/log"
	"github.com/songgao/water"
)

const (
	CON_DISCONNECT = iota
	CON_CONNECTING
	CON_CONNOK
)

// sync.Map 不需要像普通的 map 那样显式初始化。你可以直接使用它
var connUserMap sync.Map
var connAddrMap sync.Map

type ClientServer struct {
	ClientMode      int
	UUID            string
	ClientID        string
	Server          string
	ServerPort      string
	ServerTurnPort  string
	ServerTurnUser  string
	ServerTurnPass  string
	ServerTurnRealm string

	AutoConnectPassword string
	AllowTcpPortRange   []PortRange
	AllowUdpPortRange   []PortRange
	RetryConnect        bool

	AdminPort     string // 管理后台端口（默认 8099）
	AdminPassword string // 管理后台密码（Basic Auth，默认 admin）

	PublicID    string
	Mac         string
	IsConnected bool

	MyCvnIP string

	g_ifce *water.Interface
	g_conn *net.TCPConn
}

var client ClientServer

func (this ClientServer) logout() {
	//client.g_ifce.Close() //关闭网卡
	if client.g_conn == nil {
		return
	}
	err := client.g_conn.Close() //关闭连接
	log.Print(err)
}
