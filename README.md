# CTF2301B 热管理风扇控制器驱动（Linux 5.15 / QCS8550）

## 📌 项目简介

本仓库提供了一个**基于Linux5.15内核的CTF2301B热管理芯片驱动**，适配**Qualcomm QCS8550平台**。CTF2301B是一颗集成了**1路本地温度传感器 + 1路远端温度传感器 + 1路PWM风扇控制器**的系统级热管理芯片。该驱动以**I2C字符设备驱动**的形式实现，使用**工作队列**周期轮询温度与转速数据，同时通过**sysfs**暴露传感器数据和控制参数。也可在用户空间通过标准文件接口（`read/poll`）获取测量数据。

驱动已在**Linux5.15**环境中完成调试验证。

---

## ✨ 特性（Features）

* ✅ 支持CTF2301B热管理芯片（SENSYLINK）
* ✅ 基于**I2C总线**通信（7-bit地址：0x4C）
* ✅ Linux **Character Device**接口
* ✅ 使用工作队列定时轮询温度与风扇转速数据
* ✅ 使用**sysfs**暴露参数：
  * `poll_interval`：轮询周期（ms，范围150~1000）
  * `local_temp`：本地温度（12-bit，分辨率0.0625°C）
  * `remote_temp`：远端温度（13-bit，分辨率0.03125°C）
  * `rotational_speed`：风扇TACH转速计数（14-bit）
  * `mode`：风扇控制模式（`auto` / `manual`）
  * `pwm`：PWM占空比（0~255，仅manual模式下可写）
* ✅ 支持两种风扇控制模式：
  * **Auto-Temp模式**：12级LUT查找表，根据远端温度自动调节PWM
  * **Direct-DCY模式**：手动设置PWM占空比，直接控制风扇转速
* ✅ 支持阻塞/非阻塞`read()`
* ✅ 支持`poll/select`机制
* ✅ 适配**Qualcomm QCS8550**
* ✅ 使用DeviceTree进行硬件描述
* ✅ 支持模块方式加载（`.ko`）

---

## 🧩 硬件与软件环境

### 硬件平台

* SoC：**Qualcomm QCS8550**
* 芯片：**CTF2301B**（SENSYLINK Microelectronics）
* 接口：I2C / SMBus（支持Standard / Fast / Fast-mode Plus）
* 工作电压：3.0V ~ 3.6V

### 软件环境

* Linux Kernel：**5.15（Qualcomm BSP）**
* 构建环境：**Yocto**
* 编译器：aarch64-linux-gnu-gcc
* 构建方式：Kernel Module（Out-of-tree）

---

## 📊 芯片功能概要

| 功能 | 说明 |
|------|------|
| 本地温度 | 12-bit有符号，分辨率0.0625°C|
| 远端温度 | 13-bit有符号，分辨率0.03125°C|
| 温度范围 | -40°C ~ +125°C |
| PWM输出 | 频率22.7kHz~180kHz可编程，8-bit占空比 |
| 风扇TACH | 90kHz内部时钟计数，支持1/2/3脉冲转速检测 |
| LUT查找表 | 12级温度-PWM映射，支持PWM平滑过渡 |
| ALERT输出 | 可编程温度/转速报警，支持中断和比较器模式 |

---

## 🌲 DeviceTree配置示例

```dts
&i2c1 {
    status = "okay";

    ctf2301@4c {
        compatible = "sensy,ctf2301";
        reg = <0x4c>;
        status = "okay";
    };
};
```

> ⚠️ 请根据实际硬件修改I2C控制器节点及设备地址

---

## 🔧 sysfs 接口说明

驱动加载后，sysfs节点位于 `/sys/devices/.../ctf2301/` 下：

| 节点 | 权限 | 说明 |
|------|------|------|
| `poll_interval` | R/W | 轮询周期，单位ms，范围150~1000，默认200 |
| `local_temp` | R | 本地温度，单位为0.0625°C的整数倍 |
| `remote_temp` | R | 远端温度，单位为0.03125°C的整数倍 |
| `rotational_speed` | R | TACH计数原始值（可通过公式换算RPM） |
| `mode` | R/W | 风扇控制模式：`auto`（LUT自动）/ `manual`（手动PWM） |
| `pwm` | R/W | PWM占空比0~255。auto模式下只读，manual模式下可写 |

---

### 使用示例

```bash
# 查看本地温度
cat /sys/class/.../ctf2301/local_temp

# 查看当前风扇控制模式
cat /sys/class/.../ctf2301/mode

# 切换到手动模式
echo manual > /sys/class/.../ctf2301/mode

# 设置PWM占空比（0~255）
echo 128 > /sys/class/.../ctf2301/pwm

# 读取当前PWM值
cat /sys/class/.../ctf2301/pwm

# 切回自动模式（LUT控温）
echo auto > /sys/class/.../ctf2301/mode

# 修改轮询间隔为500ms
echo 500 > /sys/class/.../ctf2301/poll_interval
```

---

## ⚠️ 注意事项

* `pwm` 节点在 `auto` 模式下**写入会返回 `-EPERM`**，需先切换到 `manual` 模式
* 切换回 `auto` 模式后，PWM输出将由LUT查找表根据远端温度自动控制
* 模式切换通过寄存器0x4A的**PWPGM位（bit5）**实现，使用read-modify-write方式不影响其他配置位
* 驱动初始化时默认进入**Auto-Temp模式**，PWM频率22.5kHz，PWM平滑过渡10.9s

---

## 👤 作者

* Author: sober
* Platform: Qualcomm QCS8550
* Kernel: Linux 5.15

欢迎Issue/PR/交流讨论🙌
