#ifdef __linux__

#include "net/LinuxTun.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>

LinuxTun::~LinuxTun() { close(); }

bool LinuxTun::open(QString& ifName, const QString& ip, int prefixLen, QString& err)
{
    m_fd = ::open("/dev/net/tun", O_RDWR);
    if (m_fd < 0) {
        err = QStringLiteral("打开 /dev/net/tun 失败（需要 root / CAP_NET_ADMIN）");
        return false;
    }

    const bool tap = (m_mode == NicMode::Tap);
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    // TAP=L2 收发以太网帧；TUN=L3 收发裸 IP 包；均 IFF_NO_PI（无 4 字节前缀）
    ifr.ifr_flags = (tap ? IFF_TAP : IFF_TUN) | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, tap ? "cvntap0" : "cvn0", IFNAMSIZ - 1);

    if (::ioctl(m_fd, TUNSETIFF, &ifr) < 0) {
        err = QStringLiteral("TUNSETIFF 失败（%1）").arg(std::strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_ifName = QString::fromUtf8(ifr.ifr_name);
    ifName = m_ifName;

    // 配置地址并启用（用 ip 命令，简单可靠；需 root）。
    // TAP 模式下内核会给接口分配随机 MAC（唯一），学习式交换据此转发，无需额外设定。
    const QString addCmd = QStringLiteral("ip addr add %1/%2 dev %3 2>/dev/null")
                               .arg(ip).arg(prefixLen).arg(m_ifName);
    const QString upCmd = QStringLiteral("ip link set dev %1 up").arg(m_ifName);
    ::system(addCmd.toUtf8().constData());
    if (::system(upCmd.toUtf8().constData()) != 0)
        err = QStringLiteral("配置网卡 IP/UP 失败（需 root）");

    return true;
}

int LinuxTun::readPacket(char* buf, int maxLen)
{
    if (m_fd < 0)
        return -1;
    // 用 select 带 200ms 超时，超时返回 0，便于读循环及时检查停止标志退出
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(m_fd, &rfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    const int s = ::select(m_fd + 1, &rfds, nullptr, nullptr, &tv);
    if (s == 0)
        return 0; // 超时无数据
    if (s < 0)
        return (errno == EINTR) ? 0 : -1;
    return static_cast<int>(::read(m_fd, buf, maxLen));
}

bool LinuxTun::writePacket(const char* data, int len)
{
    if (m_fd < 0)
        return false;
    return ::write(m_fd, data, len) == len;
}

void LinuxTun::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

ITapDevice* createTapDevice(NicMode mode) { return new LinuxTun(mode); }

#endif // __linux__
