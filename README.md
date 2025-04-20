# IMEI 工具使用說明

> 本程式不提供 IMEI，使用者需要自行匯入 IMEI 前綴來進行生成。

## 介紹

這是一個用 C 語言編寫的 IMEI 處理工具，支持以下功能：
1. 匯入 IMEI 前綴及對應設備型號。
2. 根據前綴生成隨機的 IMEI。
3. 驗證 IMEI 是否有效。

## 安裝與編譯

### 安裝依賴

在 Linux 系統上，您需要安裝以下依賴：
1. `build-essential`：包含 GCC 編譯器和其他構建工具。
2. `sqlite3`：SQLite 數據庫支持。
3. `libsqlite3-dev`：SQLite 開發庫。

您可以使用以下命令安裝這些依賴：

```bash
apt update
apt install build-essential sqlite3 libsqlite3-dev
```

### 編譯

下載並解壓縮源代碼後，進入項目目錄並運行 make 命令來編譯：

```bash
make
```
此命令將編譯源代碼並生成 imei_tool 可執行文件。

## 使用說明

### 1. 启动交互式菜单

運行以下命令以進入交互式菜單：

```bash
./imei_tool
```

交互式菜單提供以下選項：

#### 1. 匯入 IMEI 前綴與型號
用來匯入 IMEI 前綴與設備型號。輸入 IMEI 前綴（8 碼）和設備型號。

#### 2. 產生隨機 IMEI
根據已匯入的 IMEI 前綴生成隨機 IMEI。您可以選擇特定的設備型號，也可以隨機生成。

#### 3. 驗證 IMEI
驗證輸入的 IMEI 是否有效。

#### 4. 離開
離開程序。

### 2. 命令行參數模式
如果您希望不通過交互式菜單，直接通過命令行操作，可以使用以下參數：

#### 2.1 匯入 CSV 文件
要將 IMEI 前綴和設備型號匯入數據庫，可以使用以下命令：

```bash
./imei_tool import imei_prefix.csv
```

此命令將從 imei_prefix.csv 文件中匯入前綴和型號。CSV 文件的格式應如下所示：

```bash
12345678,model A
89012345,model B
56789012,model C
```

#### 2.2 生成 IMEI
要生成指定數量的 IMEI，可以使用以下命令：

```bash
./imei_tool generate 10
```

這會生成 10 個隨機的 IMEI。如果您只希望為某個設備型號生成 IMEI，可以提供型號作為參數：

```bash
./imei_tool generate 5 "model A"
```

這會生成 5 個針對 "model A" 型號的 IMEI。

#### 2.3 驗證 IMEI

要驗證某個 IMEI 是否有效，可以使用以下命令：

```bash
./imei_tool validate 123456789012345
```

此命令會檢查輸入的 IMEI 是否符合 Luhn 校驗算法並且長度為 15 位。

## 注意事項
IMEI 前綴格式：IMEI 前綴必須是 8 位數字。

設備型號：設備型號是用來標識設備的字符串，可以包含空格和其他字符。

生成 IMEI：每次生成 IMEI 時，程序會隨機選擇一個已匯入的前綴，並生成剩餘的 7 位數字，最後通過 Luhn 算法計算校驗碼。
