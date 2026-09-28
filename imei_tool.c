#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "sqlite3.h"

#define IMEI_TOOL_VERSION "v1.0.3"
#define IMEI_DB "imei.db"
#define MAX_LINE 256
#define MAX_GENERATE 1000000

#if defined(_WIN32)
#define IMEI_PLATFORM "windows"
#elif defined(__APPLE__)
#define IMEI_PLATFORM "macOS"
#elif defined(__linux__)
#define IMEI_PLATFORM "linux"
#else
#define IMEI_PLATFORM "unknown"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

/* Windows 中文系统 cmd 默认代码页是 GBK(936),而本程序全部文案与 CSV 均为
 * UTF-8:启动时把控制台输入/输出代码页切到 65001(UTF-8),退出时恢复;
 * 并把宽字符命令行转成 UTF-8,使中文路径/型号参数不乱码。
 * 输出重定向到文件时无控制台,SetConsoleOutputCP 失败,字节流原样落盘(本就是 UTF-8),无害。
 * POSIX 平台不受任何影响。 */
static UINT g_old_out_cp = 0, g_old_in_cp = 0;

static void restore_console_cp(void) {
    /* atexit 在 stdio 刷新前运行:先把缓冲的 UTF-8 全部落屏,再恢复代码页 */
    fflush(stdout);
    fflush(stderr);
    if (g_old_out_cp) SetConsoleOutputCP(g_old_out_cp);
    if (g_old_in_cp)  SetConsoleCP(g_old_in_cp);
}

/* Ctrl+C / Ctrl+Break / 关闭窗口时,默认处理器直接终止进程、不经过 atexit,
 * 控制台代码页会遗留为 65001。此处抢在默认处理器之前恢复代码页。
 * 不调用 fflush:控制台处理器运行在独立线程,对 stdio 缓冲加锁有死锁风险,
 * 丢弃尾部缓冲是可接受的折衷。 */
static BOOL WINAPI ctrl_restore_cp(DWORD type) {
    (void)type;
    if (g_old_out_cp) SetConsoleOutputCP(g_old_out_cp);
    if (g_old_in_cp)  SetConsoleCP(g_old_in_cp);
    return FALSE; /* 交回默认处理器终止进程 */
}

/* 把宽字符 argv 逐个转成 UTF-8;失败返回 NULL(调用方退回原始 argv,功能仍可用) */
static char **win_utf8_argv(int argc, char **argv) {
    (void)argv;
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || wargc != argc) {
        LocalFree(wargv);
        return NULL;
    }
    char **u8 = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (!u8) { LocalFree(wargv); return NULL; }
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        if (n <= 0 || !(u8[i] = (char *)malloc((size_t)n)) ||
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, u8[i], n, NULL, NULL) <= 0) {
            for (int j = 0; j < argc; ++j) free(u8[j]);
            free(u8);
            LocalFree(wargv);
            return NULL;
        }
    }
    LocalFree(wargv);
    return u8;
}
#endif

int init_db(sqlite3 **db);
int import_prefix(sqlite3 *db, const char *prefix, const char *model);
int import_prefix_csv(sqlite3 *db, const char *filepath);
char* generate_imei(sqlite3 *db, const char *model);
int validate_imei(const char *imei);
int luhn_checksum(const char *imei14);
void menu(sqlite3 *db);
static int valid_utf8(const char *s);

/* 丢弃 stdin 当前行剩余字符(含行尾换行);EOF 时立即返回 */
static void drain_line(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

/* 菜单内 stdin 结束(EOF)时的统一退出路径 */
static void menu_eof_exit(void) {
    printf("\n(输入已结束,离开)\n");
}

void print_usage(void) {
    fprintf(stderr, "用法:\n");
    fprintf(stderr, "  ./imei_tool                  启动互动模式\n");
    fprintf(stderr, "  ./imei_tool generate <数量> [型号]\n");
    fprintf(stderr, "  ./imei_tool validate <imei>\n");
    fprintf(stderr, "  ./imei_tool import <csv档>\n");
    fprintf(stderr, "  ./imei_tool version          显示版本\n");
    fprintf(stderr, "\n退出码:0 成功;1 运行失败(验证不通过/导入失败/数据库错误);2 用法错误\n");
    fprintf(stderr, "本工具仅供软件开发测试与学习研究,禁止用于伪造真实设备标识。\n");
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    g_old_out_cp = GetConsoleOutputCP();
    g_old_in_cp  = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    atexit(restore_console_cp);
    SetConsoleCtrlHandler(ctrl_restore_cp, TRUE);
    char **u8argv = win_utf8_argv(argc, argv);
    if (u8argv) argv = u8argv;
#endif
    if (argc == 2 && (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0)) {
        printf("imei_tool %s (%s)\n", IMEI_TOOL_VERSION, IMEI_PLATFORM);
        return 0;
    }

    sqlite3 *db = NULL;
    if (init_db(&db) != SQLITE_OK || !db) {
        fprintf(stderr, "无法初始化资料库:%s\n", db ? sqlite3_errmsg(db) : "sqlite3_open 失败");
        if (db) sqlite3_close(db);
        return 1;
    }

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    srand(seed);

    if (argc == 1) {
        menu(db);
    } else if ((argc == 3 || argc == 4) && strcmp(argv[1], "generate") == 0) {
        char *end = NULL;
        long count = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || count < 0 || count > MAX_GENERATE) {
            fprintf(stderr, "数量无效:%s(须为 0-%d 的整数)\n", argv[2], MAX_GENERATE);
            sqlite3_close(db);
            return 2;
        }
        const char *model = argc == 4 ? argv[3] : NULL;
        for (long i = 0; i < count; ++i) {
            char *imei = generate_imei(db, model);
            if (imei) {
                printf("%s\n", imei);
                free(imei);
            } else {
                fprintf(stderr, "产生失败。\n");
                sqlite3_close(db);
                return 1;
            }
        }
    } else if (argc == 3 && strcmp(argv[1], "validate") == 0) {
        if (strlen(argv[2]) == 15 && validate_imei(argv[2])) {
            printf("IMEI 验证通过。\n");
        } else {
            printf("IMEI 验证失败。\n");
            sqlite3_close(db);
            return 1;
        }
    } else if (argc == 3 && strcmp(argv[1], "import") == 0) {
        if (import_prefix_csv(db, argv[2]) == 0)
            printf("导入完成。\n");
        else {
            sqlite3_close(db);
            return 1;
        }
    } else {
        print_usage();
        sqlite3_close(db);
        return 2;
    }

    sqlite3_close(db);
    return 0;
}

void menu(sqlite3 *db) {
    int choice;
    char input[64], model[64];

    while (1) {
        printf("\n=== IMEI 工具 ===\n");
        printf("1. 导入 IMEI 前缀与型号\n");
        printf("2. 产生随机 IMEI\n");
        printf("3. 验证 IMEI\n");
        printf("4. 离开\n");
        printf("请选择: ");
        int rc = scanf("%d", &choice);
        if (rc == EOF) { menu_eof_exit(); return; }
        if (rc != 1) {
            // 清空残留在输入, 无效选项回显
            drain_line();
            printf("无效选项。\n");
            continue;
        }

        switch (choice) {
            case 1: {
                printf("输入 IMEI 前缀 (8 码): ");
                if (scanf("%63s", input) != 1) {
                    if (feof(stdin)) { menu_eof_exit(); return; }
                    drain_line(); break;
                }
                printf("输入设备型号: ");
                if (scanf(" %63[^\n]", model) != 1) {
                    if (feof(stdin)) { menu_eof_exit(); return; }
                    drain_line(); break;
                }
                // 去型号首尾空白
                char *m2 = model;
                while (*m2==' '||*m2=='\t') m2++;
                char *me = m2 + strlen(m2);
                while (me>m2 && (me[-1]==' '||me[-1]=='\t')) *--me=0;
                // 前缀必须 8 位纯数字
                int ok = (strlen(input)==8);
                if (ok) for (char *p=input; *p; ++p) if(!(*p>='0'&&*p<='9')){ok=0;break;}
                if (!ok) {
                    printf("前缀需为 8 位数字。\n");
                } else if (!valid_utf8(m2)) {
                    printf("型号包含非法字符(非 UTF-8 编码)。\n");
                } else {
                    int rc = import_prefix(db, input, m2);
                    if (rc == 0)
                        printf("导入成功。\n");
                    else if (rc == 1)
                        printf("该前缀已存在,未重复写入。\n");
                    else
                        printf("导入失败:%s\n", sqlite3_errmsg(db));
                }
                break;
            }
            case 2: {
                printf("输入设备型号 (可留空): ");
                // scanf("%d") 后残留换行; 用循环清空 stdin, 避免 getchar 吃有效输入
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF) {}
                if (!fgets(model, sizeof(model), stdin)) { menu_eof_exit(); return; }
                model[strcspn(model, "\n")] = 0;  // 去除换行
                char *imei = generate_imei(db, strlen(model) > 0 ? model : NULL);
                if (imei) {
                    printf("产生的 IMEI：%s\n", imei);
                    free(imei);
                } else {
                    printf("产生失败，可能无前缀。\n");
                }
                break;
            }
            case 3:
                printf("输入 IMEI (15 码): ");
                if (scanf("%63s", input) != 1) {
                    if (feof(stdin)) { menu_eof_exit(); return; }
                    drain_line(); break;
                }
                if (strlen(input) == 15) {
                    if (validate_imei(input))
                        printf("IMEI 验证通过。\n");
                    else
                        printf("IMEI 验证失败。\n");
                } else {
                    printf("IMEI 长度需为 15 码。\n");
                }
                break;
            case 4:
                printf("再见！\n");
                return;
            default:
                printf("无效选项。\n");
        }
    }
}

int init_db(sqlite3 **db) {
    int rc = sqlite3_open(IMEI_DB, db);
    if (rc != SQLITE_OK) return rc;

    /* 并发场景(如另一实例正在导入)下等待锁释放,而非立即报错 */
    sqlite3_busy_timeout(*db, 3000);

    const char *sql = "CREATE TABLE IF NOT EXISTS imei_prefix (id INTEGER PRIMARY KEY AUTOINCREMENT, prefix TEXT UNIQUE, model TEXT);";
    return sqlite3_exec(*db, sql, 0, 0, 0);
}

/* 返回:0 = 新入库;1 = 前缀已存在,未写入;2 = 数据库错误 */
int import_prefix(sqlite3 *db, const char *prefix, const char *model) {
    const char *sql = "INSERT OR IGNORE INTO imei_prefix (prefix, model) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return 2;

    sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, model, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 2;

    /* INSERT OR IGNORE 命中已存在前缀时语句成功但未改任何行 */
    return sqlite3_changes(db) == 0 ? 1 : 0;
}

/* 校验字符串是否为合法 UTF-8(RFC 3629):拒绝 GBK 等其他编码、
 * 过长编码、代理区与超 U+10FFFF 序列,避免乱码静默入库 */
static int valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char c = *p;
        if (c < 0x80) { p++; continue; }
        int extra;
        unsigned char lo = 0x80, hi = 0xBF;  /* 首个连续字节的合法窗口 */
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            if (c < 0xC2) return 0;          /* 过长编码 C0/C1 */
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            if (c == 0xE0) lo = 0xA0;        /* 排除过长 E0 80-9F */
            else if (c == 0xED) hi = 0x9F;   /* 排除代理区 ED A0-BF */
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            if (c > 0xF4) return 0;                 /* F5-FF:超 U+10FFFF,非法首字节 */
            if (c == 0xF0) lo = 0x90;        /* 排除过长 F0 80-8F */
            else if (c == 0xF4) hi = 0x8F;   /* 排除超 U+10FFFF */
        } else {
            return 0;                         /* 孤立连续字节或非法首字节(F5-FF) */
        }
        p++;
        if (*p < lo || *p > hi) return 0;     /* 首个连续字节(含字符串提前结束) */
        p++;
        for (int i = 1; i < extra; ++i) {
            if ((*p & 0xC0) != 0x80) return 0;
            p++;
        }
    }
    return 1;
}

int import_prefix_csv(sqlite3 *db, const char *filepath) {
    FILE *fp;
#ifdef _WIN32
    /* argv 已统一转成 UTF-8,而 ANSI 版 fopen 只认系统代码页(中文系统是 GBK);
     * 中文路径须转宽字符走 _wfopen。 */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    wchar_t *wpath = (wlen > 0) ? (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t)) : NULL;
    if (wpath) {
        MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);
        fp = _wfopen(wpath, L"r");
        free(wpath);
    } else {
        fp = NULL;
    }
#else
    fp = fopen(filepath, "r");
#endif
    if (!fp) {
        perror("开启 CSV 失败");
        return 1;
    }

    /* 整个导入包一个事务:逐行 autocommit 时每行一次 fsync,
     * 万行级 CSV 会慢出两个数量级;出错整体回滚,不留半套数据 */
    if (sqlite3_exec(db, "BEGIN", 0, 0, 0) != SQLITE_OK) {
        fprintf(stderr, "无法开始事务:%s\n", sqlite3_errmsg(db));
        fclose(fp);
        return 1;
    }

    char line[MAX_LINE];
    int success = 0, existed = 0, total = 0, skipped = 0, db_errors = 0, cr_warned = 0;
    const char *first_err = NULL;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        int toolong = 0;
        if (len == sizeof(line) - 1 && line[len-1] != '\n') {
            /* 行长超过缓冲:读掉本行剩余部分,整行跳过(不截断入库)。
             * fgetc 返回 EOF 说明这是文件末尾无换行的完整行,正常处理。 */
            int c = fgetc(fp);
            if (c == EOF) {
                /* 完整行 */
            } else {
                while (c != '\n' && c != EOF) c = fgetc(fp);
                toolong = 1;
            }
        }
        total++;
        if (toolong) { skipped++; continue; }

        /* UTF-8 BOM(仅可能出现在首行):剥掉再解析 */
        if (total == 1 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            memmove(line, line + 3, strlen(line + 3) + 1);
        }
        /* 孤立 CR 后还跟内容:文件是 CR(旧 Mac)行尾,fgets 会把整文件读成一行,
         * 静默丢掉首个记录之外的全部内容。此处整行跳过并警告,让用户转换行尾。 */
        char *cr = strchr(line, '\r');
        if (cr && cr[1] != '\n' && cr[1] != '\0') {
            if (!cr_warned) {
                fprintf(stderr, "警告:侦测到 CR(旧 Mac)行尾,仅支持 LF/CRLF;"
                                "该行已整行跳过,请转换行尾后重新导入\n");
                cr_warned = 1;
            }
            skipped++;
            continue;
        }
        line[strcspn(line, "\r\n")] = 0;   // 去掉行尾 CR/LF
        // 找第一个有效分隔符; 型号只允许含简单字符(防注入逗号截断)
        char *delim = strpbrk(line, ",;");
        if (!delim) { skipped++; continue; }
        *delim = 0;
        char *prefix = line;
        char *model = delim + 1;
        // 去首尾空白
        while (*prefix == ' ' || *prefix == '\t') prefix++;
        char *pe = prefix + strlen(prefix);
        while (pe > prefix && (pe[-1] == ' ' || pe[-1] == '\t')) *--pe = 0;
        while (*model == ' ' || *model == '\t') model++;
        char *me = model + strlen(model);
        while (me > model && (me[-1] == ' ' || me[-1] == '\t')) *--me = 0;

        int prefix_ok = (strlen(prefix) == 8);
        for (const unsigned char *p = (const unsigned char*)prefix; *p; ++p)
            if (!(*p >= '0' && *p <= '9')) prefix_ok = 0;

        if (prefix_ok && *model && valid_utf8(model)) {
            int rc = import_prefix(db, prefix, model);
            if (rc == 0)      success++;
            else if (rc == 1) existed++;
            else {
                skipped++;
                db_errors++;
                if (!first_err) first_err = sqlite3_errmsg(db);
            }
        } else {
            skipped++;
        }
    }

    if (sqlite3_exec(db, "COMMIT", 0, 0, 0) != SQLITE_OK) {
        fprintf(stderr, "提交失败:%s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        fclose(fp);
        return 1;
    }
    fclose(fp);
    printf("导入完成:新入库 %d / 已存在 %d / 无效跳过 %d(共 %d 行)\n",
           success, existed, skipped, total);
    if (db_errors > 0)
        fprintf(stderr, "警告:%d 条因数据库错误未入库(首个错误:%s)\n", db_errors, first_err);
    if (success == 0 && existed == 0)
        return 1;   /* 新入库与已存在都为 0:全部无效或库不可写,脚本需要能感知 */
    return 0;
}

/* 真实 RBI 分配表(TAC 前 2 位 = Reporting Body Identifier,GSMA 分配)。
 * 仅收录有真实国家归属的代码;00/02-09(测试码)、10(DECT)、30(Iridium)、
 * 98(保留)、99(GHA 国际)不属于国家码,不参与随机合成。 */
static const struct { char code[3]; const char *country; } REAL_RBIS[] = {
    {"01", "美国/PTCRB"},        {"35", "英国/BABT"},
    {"86", "中国/TAF"},          {"91", "印度/MSAI"},
    {"33", "法国/DGPT"},         {"44", "英国/BABT"},
    {"45", "丹麦/NTA"},          {"49", "德国/BZT"},
    {"50", "德国/BZT ETS"},      {"51", "德国/Cetecom ICT"},
    {"52", "德国/Cetecom"},      {"53", "德国/TUV"},
    {"54", "德国/Phoenix Test Lab"},
};

char* generate_imei(sqlite3 *db, const char *model) {
    const char *sql = model ?
        "SELECT prefix FROM imei_prefix WHERE model = ? ORDER BY RANDOM();" :
        "SELECT prefix FROM imei_prefix ORDER BY RANDOM() LIMIT 1;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return NULL;

    if (model) sqlite3_bind_text(stmt, 1, model, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    const unsigned char *prefix = (rc == SQLITE_ROW) ? sqlite3_column_text(stmt, 0) : NULL;
    int plen = (rc == SQLITE_ROW) ? sqlite3_column_bytes(stmt, 0) : 0;
    /* 最后防线:库里被手工改坏的前缀(非 8 位纯数字)不用,绝不产出垃圾 IMEI */
    int prefix_ok = (prefix != NULL && plen == 8);
    if (prefix_ok) {
        for (int i = 0; i < 8; ++i)
            if (prefix[i] < '0' || prefix[i] > '9') { prefix_ok = 0; break; }
    }

    char imei14[15];
    if (prefix_ok) {
        memcpy(imei14, prefix, 8);  // 复制库中前缀
        sqlite3_finalize(stmt);
        for (int i = 8; i < 14; ++i)  // 生成剩余部分
            imei14[i] = '0' + (rand() % 10);
    } else {
        sqlite3_finalize(stmt);
        if (model != NULL)
            return NULL;   /* 指定了型号但未找到(或该型号前缀损坏):明确失败,不静默换随机前缀 */
        /* 未指定前缀(空库/库中前缀全损坏):合成随机前缀。
         * 前 2 位从真实 RBI 分配表抽取,保证国家码真实存在 */
        static int hinted = 0;
        if (!hinted) {
            hinted = 1;
            fprintf(stderr, "(库中无可用前缀,已随机合成真实国家码前缀)\n");
        }
        const char *rbi = REAL_RBIS[rand() % (int)(sizeof(REAL_RBIS) / sizeof(REAL_RBIS[0]))].code;
        imei14[0] = rbi[0];
        imei14[1] = rbi[1];
        for (int i = 2; i < 14; ++i)
            imei14[i] = '0' + (rand() % 10);
    }
    imei14[14] = '\0';  // 结束符

    int check_digit = luhn_checksum(imei14);  // 计算校验码
    char *imei15 = malloc(16);
    if (!imei15) return NULL;
    snprintf(imei15, 16, "%s%d", imei14, check_digit);  // 生成完整 IMEI

    return imei15;
}

int validate_imei(const char *imei) {
    if (strlen(imei) != 15) return 0;
    for (int i = 0; i < 15; i++)
        if (imei[i] < '0' || imei[i] > '9') return 0;
    char imei14[15];
    strncpy(imei14, imei, 14);
    imei14[14] = '\0';

    int check_digit = luhn_checksum(imei14);
    return (check_digit == (imei[14] - '0'));
}

int luhn_checksum(const char *imei14) {
    int sum = 0;
    for (int i = 0; i < 14; i++) {
        char c = imei14[i];
        if (c < '0' || c > '9') return -1;   // 非数字
        int digit = c - '0';
        if (i % 2 == 1) digit *= 2;
        if (digit > 9) digit -= 9;
        sum += digit;
    }
    return (10 - (sum % 10)) % 10;
}
