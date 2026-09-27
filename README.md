# IMEI 工具使用说明

> 本程序不提供 IMEI,使用者需要自行导入 IMEI 前缀来进行生成。

## 介绍

这是一个用 C 语言编写的 IMEI 处理工具,支持以下功能:

1. 导入 IMEI 前缀及对应设备型号
2. 根据前缀生成随机的 IMEI
3. 验证 IMEI 是否有效

数据存在本地 SQLite 数据库(`imei.db`)中。

## 安装与编译

### 安装依赖

在 Linux 系统上,需要安装以下依赖:

1. `build-essential`:包含 GCC 编译器和其他构建工具
2. `sqlite3`:SQLite 数据库支持
3. `libsqlite3-dev`:SQLite 开发库

```bash
apt update
apt install build-essential sqlite3 libsqlite3-dev
```

### 编译

下载并解压源代码后,进入项目目录并运行 make 命令来编译:

```bash
make
```

此命令将编译源代码并生成 `imei_tool` 可执行文件。

## 使用说明

### 1. 启动交互式菜单

```bash
./imei_tool
```

交互式菜单提供以下选项:

#### 1. 导入 IMEI 前缀与型号
输入 IMEI 前缀(8 位)和设备型号。

#### 2. 产生随机 IMEI
根据已导入的 IMEI 前缀生成随机 IMEI。可以指定设备型号,也可以留空随机选择。

#### 3. 验证 IMEI
验证输入的 IMEI 是否有效。

#### 4. 离开
退出程序。

### 2. 命令行参数模式

#### 2.1 导入 CSV 文件

```bash
./imei_tool import imei_prefix.csv
```

CSV 文件格式(分隔符支持逗号 `,` 或分号 `;`):

```csv
12345678,model A
89012345,model B
56789012,model C
```

#### 2.2 生成 IMEI

```bash
./imei_tool generate 10
```

生成 10 个随机 IMEI。只为某个设备型号生成:

```bash
./imei_tool generate 5 "model A"
```

#### 2.3 验证 IMEI

```bash
./imei_tool validate 123456789012345
```

检查输入的 IMEI 是否符合 Luhn 校验算法并且长度为 15 位。

## 注意事项

- **IMEI 前缀格式**:必须是 8 位纯数字
- **设备型号**:标识设备的字符串,可以包含空格和其他字符
- **生成规则**:随机选择一个已导入的前缀,生成 6 位随机数字,最后通过 Luhn 算法计算第 15 位校验码(8 位前缀 + 6 位随机 + 1 位校验)
- **CSV 导入**:格式错误或前缀不合法的行会自动跳过,导入完成时显示成功/跳过数量
- **重复前缀**:数据库中已存在的前缀会被忽略(`INSERT OR IGNORE`)
- **空库生成**:没有导入任何前缀时生成会失败(提示"产生失败")

## 许可证

MIT License(见 [LICENSE](LICENSE))
