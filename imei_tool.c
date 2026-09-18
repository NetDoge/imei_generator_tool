#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "sqlite3.h"

#define IMEI_DB "imei.db"
#define MAX_LINE 256

int init_db(sqlite3 **db);
int import_prefix(sqlite3 *db, const char *prefix, const char *model);
int import_prefix_csv(sqlite3 *db, const char *filepath);
char* generate_imei(sqlite3 *db, const char *model);
int validate_imei(const char *imei);
int luhn_checksum(const char *imei14);
void menu(sqlite3 *db);

void print_usage() {
    printf("用法:\n");
    printf("  ./imei_tool                  啟動互動模式\n");
    printf("  ./imei_tool generate <數量> [型號]\n");
    printf("  ./imei_tool validate <imei>\n");
    printf("  ./imei_tool import <csv檔>\n");
}

int main(int argc, char *argv[]) {
    sqlite3 *db;
    if (init_db(&db) != SQLITE_OK) {
        fprintf(stderr, "無法初始化資料庫\n");
        return 1;
    }

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    srand(seed);

    if (argc == 1) {
        menu(db);
    } else if ((argc == 3 || argc == 4) && strcmp(argv[1], "generate") == 0) {
        int count = atoi(argv[2]);
        const char *model = argc == 4 ? argv[3] : NULL;
        for (int i = 0; i < count; ++i) {
            char *imei = generate_imei(db, model);
            if (imei) {
                printf("%s\n", imei);
                free(imei);
            } else {
                fprintf(stderr, "產生失敗。\n");
                break;
            }
        }
    } else if (argc == 3 && strcmp(argv[1], "validate") == 0) {
        if (strlen(argv[2]) == 15 && validate_imei(argv[2]))
            printf("IMEI 驗證通過。\n");
        else
            printf("IMEI 驗證失敗。\n");
    } else if (argc == 3 && strcmp(argv[1], "import") == 0) {
        if (import_prefix_csv(db, argv[2]) == 0)
            printf("匯入完成。\n");
        else
            printf("匯入失敗。\n");
    } else {
        print_usage();
    }

    sqlite3_close(db);
    return 0;
}

void menu(sqlite3 *db) {
    int choice;
    char input[64], model[64];

    while (1) {
        printf("\n=== IMEI 工具 ===\n");
        printf("1. 匯入 IMEI 前綴與型號\n");
        printf("2. 產生隨機 IMEI\n");
        printf("3. 驗證 IMEI\n");
        printf("4. 離開\n");
        printf("請選擇: ");
        int rc = scanf("%d", &choice);
        if (rc != 1) {
            // 清空残留在输入, 无效选项回显
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            printf("無效選項。\n");
            continue;
        }

        switch (choice) {
            case 1: {
                printf("輸入 IMEI 前綴 (8 碼): ");
                if (scanf("%63s", input) != 1) { while(getchar()!='\n'&&getchar()!=EOF); break; }
                printf("輸入設備型號: ");
                if (scanf(" %63[^\n]", model) != 1) { while(getchar()!='\n'&&getchar()!=EOF); break; }
                // 去型号首尾空白
                char *m2 = model;
                while (*m2==' '||*m2=='\t') m2++;
                char *me = m2 + strlen(m2);
                while (me>m2 && (me[-1]==' '||me[-1]=='\t')) *--me=0;
                // 前缀必须 8 位纯数字
                int ok = (strlen(input)==8);
                if (ok) for (char *p=input; *p; ++p) if(!(*p>='0'&&*p<='9')){ok=0;break;}
                if (ok) {
                    if (import_prefix(db, input, m2) == 0)
                        printf("匯入成功。\n");
                    else
                        printf("匯入失敗或已存在。\n");
                } else {
                    printf("前綴需為 8 位數字。\n");
                }
                break;
            }
            case 2: {
                printf("輸入設備型號 (可留空): ");
                // scanf("%d") 后残留换行; 用循环清空 stdin, 避免 getchar 吃有效输入
                int ch;
                while ((ch = getchar()) != '\n' && ch != EOF) {}
                fgets(model, sizeof(model), stdin);
                model[strcspn(model, "\n")] = 0;  // 去除換行
                char *imei = generate_imei(db, strlen(model) > 0 ? model : NULL);
                if (imei) {
                    printf("產生的 IMEI：%s\n", imei);
                    free(imei);
                } else {
                    printf("產生失敗，可能無前綴。\n");
                }
                break;
            }
            case 3:
                printf("輸入 IMEI (15 碼): ");
                if (scanf("%63s", input) != 1) { while(getchar()!='\n'&&getchar()!=EOF); break; }
                if (strlen(input) == 15) {
                    if (validate_imei(input))
                        printf("IMEI 驗證通過。\n");
                    else
                        printf("IMEI 驗證失敗。\n");
                } else {
                    printf("IMEI 長度需為 15 碼。\n");
                }
                break;
            case 4:
                printf("再見！\n");
                return;
            default:
                printf("無效選項。\n");
        }
    }
}

int init_db(sqlite3 **db) {
    int rc = sqlite3_open(IMEI_DB, db);
    if (rc != SQLITE_OK) return rc;

    const char *sql = "CREATE TABLE IF NOT EXISTS imei_prefix (id INTEGER PRIMARY KEY AUTOINCREMENT, prefix TEXT UNIQUE, model TEXT);";
    return sqlite3_exec(*db, sql, 0, 0, 0);
}

int import_prefix(sqlite3 *db, const char *prefix, const char *model) {
    const char *sql = "INSERT OR IGNORE INTO imei_prefix (prefix, model) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, model, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? 0 : rc;
}

int import_prefix_csv(sqlite3 *db, const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        perror("開啟 CSV 失敗");
        return 1;
    }

    char line[MAX_LINE];
    int success = 0, total = 0, skipped = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;   // 去掉行尾 CR/LF
        total++;
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

        if (prefix_ok && *model) {
            if (import_prefix(db, prefix, model) == 0) success++;
            else skipped++;
        } else {
            skipped++;
        }
    }

    fclose(fp);
    printf("匯入完成：%d / %d 條成功，%d 條跳過\n", success, total, skipped);
    return 0;
}

char* generate_imei(sqlite3 *db, const char *model) {
    const char *sql = model ?
        "SELECT prefix FROM imei_prefix WHERE model = ? ORDER BY RANDOM();" :
        "SELECT prefix FROM imei_prefix ORDER BY RANDOM() LIMIT 1;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return NULL;

    if (model) sqlite3_bind_text(stmt, 1, model, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    const unsigned char *prefix = sqlite3_column_text(stmt, 0);
    char imei14[15];
    if (!prefix || sqlite3_column_bytes(stmt, 0) < 8) {
        sqlite3_finalize(stmt);
        return NULL;
    }
    strncpy(imei14, (const char*)prefix, 8);  // 复制前缀
    imei14[8] = 0;  // 确保截断
    for (int i = 8; i < 14; ++i) {  // 生成剩余部分
        imei14[i] = '0' + (rand() % 10);
    }
    imei14[14] = '\0';  // 结束符

    int check_digit = luhn_checksum(imei14);  // 计算校验码
    char *imei15 = malloc(16);
    snprintf(imei15, 16, "%s%d", imei14, check_digit);  // 生成完整 IMEI

    sqlite3_finalize(stmt);
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
