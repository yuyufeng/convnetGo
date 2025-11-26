package main

import (
	"fmt"
	"net"
	"os/exec"
	"runtime"
	"strings"
	"time"

	"github.com/labstack/gommon/log"
	"github.com/songgao/water"
)

func setupIfce(ipNet net.IPNet, dev string) error {
	var cmd *exec.Cmd
	if runtime.GOOS == "windows" {
		sargs := fmt.Sprintf("interface ip set address name='REPLACE_ME' source=static addr=REPLACE_ME mask=REPLACE_ME gateway=none")
		args := strings.Split(sargs, " ")
		args[4] = fmt.Sprintf("name=%s", dev)
		args[6] = fmt.Sprintf("addr=%s", ipNet.IP)
		args[7] = fmt.Sprintf("mask=%d.%d.%d.%d", ipNet.Mask[0], ipNet.Mask[1], ipNet.Mask[2], ipNet.Mask[3])
		cmd = exec.Command("netsh", args...)
		log.Info("cmdexec: ", cmd.String())
		if err := cmd.Run(); err != nil {
			log.Errorf("Failed to assign IP address: %v", err)
			return fmt.Errorf("设置IP地址失败: %v", err)
		}

		// 验证IP是否设置成功
		interfaces, err := net.Interfaces()
		if err != nil {
			return fmt.Errorf("获取网络接口失败: %v", err)
		}

		for _, iface := range interfaces {
			if iface.Name == dev {
				addrs, err := iface.Addrs()
				if err != nil {
					return fmt.Errorf("获取接口地址失败: %v", err)
				}
				for _, addr := range addrs {
					if ipnet, ok := addr.(*net.IPNet); ok {
						if ipnet.IP.Equal(ipNet.IP) {
							log.Info("IP设置成功:", ipnet.IP)
							return nil
						}
					}
				}
			}
		}
		return fmt.Errorf("IP地址验证失败，未找到设置的IP: %v", ipNet.IP)
	} else if runtime.GOOS == "linux" {
		ipAddr := ipNet.IP.String()
		ipMask := net.IP(ipNet.Mask).String()
		cmd = exec.Command("ip", "addr", "add", fmt.Sprintf("%s/%s", ipAddr, ipMask), "dev", dev)
		log.Info("cmdexec: ", cmd.String())
		if err := cmd.Run(); err != nil {
			log.Errorf("Failed to assign IP address: %v", err)
			return fmt.Errorf("设置IP地址失败: %v", err)
		}

		//启动网卡
		time.Sleep(time.Second * 1)
		cmd = exec.Command("ip", "link", "set", dev, "up")
		log.Info("cmdexec: ", cmd.String())
		if err := cmd.Run(); err != nil {
			log.Errorf("Failed to bring up the interface: %v", err)
			return fmt.Errorf("启动网卡失败: %v", err)
		}

		// 验证IP是否设置成功
		cmd = exec.Command("ip", "addr", "show", "dev", dev)
		output, err := cmd.Output()
		if err != nil {
			return fmt.Errorf("获取接口信息失败: %v", err)
		}

		if !strings.Contains(string(output), ipAddr) {
			return fmt.Errorf("IP地址验证失败，未找到设置的IP: %v", ipAddr)
		}
		log.Info("IP设置成功:", ipAddr)
		return nil
	} else {
		log.Info("Unsupported OS:", runtime.GOOS)
		return fmt.Errorf("不支持的操作系统: %v", runtime.GOOS)
	}
}

func setupArpinfo(mac, ip string) {
	if runtime.GOOS == "windows" {
		cmd := exec.Command("arp", "-d", "*")
		if err := cmd.Run(); err != nil {
			log.Info(err)
		}
		//将:替换为-
		mac = strings.Replace(mac, ":", "-", -1)
		cmd = exec.Command("arp", "-s", ip, mac)
	}
	if runtime.GOOS == "linux" {
		cmd := exec.Command("arp", "-s", ip, mac)
		if err := cmd.Run(); err != nil {
			log.Info(err)
		}
	}
}

func teardownIfce(ifce *water.Interface) {
	client.g_ifce = nil
	if err := ifce.Close(); err != nil {
		log.Info(err)
	}
}
