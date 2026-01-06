# explorer-esp

> ⚠️ **注意：本仓库代码仅供参考与学习**
> 🤖 **代码与自述文件均有AI辅助编写**

## 简介

这是一个 **AnyShake Explorer 的 ESP32 第三方实现方案**。
通过 **ESP32 + MPU6050** 采集加速度数据，并上传至 **AnyShake Observer**。

该方案具有以下特点：

* 💰 **成本极低**
* 📦 **体积小、占用空间少**
* 🔧 结构简单，便于 DIY 与学习

整体流程如下：

> **校准 → 放大 → 上传**

需要注意的是：

* 采集数据**无法与专业测站相比**
* **未经过真实地震环境测试**
* 数据的准确性与可靠性 **无法保证**
* **仅供学习与研究参考**

## 数据协议说明

本方案基于 AnyShake 官方文档中的
[`protocol-v1-legacy`](https://anyshake.org/docs/anyshake-explorer/Data%20Protocol/protocol-v1-legacy) 实现。

已知问题：

* 在数据上传过程中 **可能存在少量 Bug**
* 实际长时间运行测试中，**会出现 N 次 XOR 校验错误（截图P1）**
* 不影响基本使用（但是历史资料->数据导出会出问，题截图P2），但仍存在改进空间

P1:

![截图p1](./img/截图4.png)

P2:

![截图p2](./img/截图3.png)

👉 如果你对该协议或代码优化感兴趣，**非常欢迎提交 PR 或参与修复**！


## 硬件连接

### 所需硬件

* ESP32
* MPU6050
* 杜邦线 / 面包板（推荐）

### 连接方式

请按照下图方式连接 ESP32 与 MPU6050：

![硬件连接示意图](./img/ESP32+MPU6050.png)

建议：

* 使用 **面包板** 可减小整体体积并方便固定
* 由于代码中包含 **位置校准逻辑**，传感器可自由放置于：

  * 墙面
  * 地板
  * 天花板等位置

## 烧录与使用

### 固件烧录

* `Releases` 中提供 **已编译好的固件（`.bin` 格式）**
* 使用常见 ESP32 烧录工具直接刷入即可

### 设备部署

1. 将 ESP32 固定在 **干扰尽可能小的位置**
2. 使用 USB 数据线连接至：

   * 电脑
   * 服务器
   * 树莓派等 ARM 设备

## Observer 安装与配置

### 下载 Observer

Observer 官方仓库：

* 👉 [AnyShake Observer GitHub](https://github.com/anyshake/observer)

在 `Releases` 中：

* 根据 **上位机架构** 下载对应的 `zip` 文件

安装教程可参考官方文档：

* 📘 [AnyShake Observer 安装指南](https://anyshake.org/docs/anyshake-observer/installing-observer)

### 配置 Observer

1. 解压 Observer 文件
2. 将 `assets/config.json` 移动到 **Observer 程序所在目录**
3. 打开该目录下的 `config.json` 进行配置

推荐使用官方配置工具：

* 🛠️ [AnyShake Prisma](https://anyshake.github.io/prisma/)

#### 关键配置项说明

* **Data Protocol**

  * 必须选择：`v2`（用于兼容本方案的数据格式）

* **Serial Port**

  * Windows：`COM{端口号}`
  * Linux：`/dev/ttyUSB0`
  * ⚠️ 只填写 `//` 后的部分即可，无需删除前缀

* **Database**

  * 可选择：`SQLite`
  * 示例路径：

    ```json
    "./data.db"
    ```
  * Observer 会自动创建数据库文件

### 启动

确认：

* ESP32 正常连接
* Observer 配置正确

启动 Observer 后即可开始采集数据：

![Observer 运行示例](./img/截图1.png)
![Observer 运行示例](./img/截图2.png)

## 常见问题

### Linux 无法启动 Observer

请检查文件是否具有执行权限：

```bash
chmod +x observer
```
