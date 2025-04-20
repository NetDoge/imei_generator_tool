#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>

#define IMEI_DB "imei.db"

void menu() {
    printf("\nIMEI 工具選單:\n");
    printf("1. 導入 IMEI 前綴 CSV\n");
    printf("2. 生成 IMEI\n");
    printf("3. 校驗 IMEI\n");
    printf("4. 退出\n");
}

int import_prefix(sqlite3 *db, const char *prefix, const char *model) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO imei_prefix (prefix, model) VALUES (?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        printf("SQL 錯誤: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, model, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("插入資料失敗: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int import_prefix_csv(sqlite3 *db, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("無法打開文件 %s\n", filename);
        return 1;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *prefix = strtok(line, ",");
        char *model = strtok(NULL, "\n");
        if (prefix && model) {
            import_prefix(db, prefix, model);
        }
    }

    fclose(file);
    return 0;
}

char* generate_imei(sqlite3 *db, const char *model) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT prefix FROM imei_prefix WHERE model = ? ORDER BY RANDOM() LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        printf("SQL 錯誤: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, model, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    const char *prefix = (const char*)sqlite3_column_text(stmt, 0);
    char *imei = (char*)malloc(16 * sizeof(char));
    snprintf(imei, 16, "%s", prefix);

    // 添加校驗位
    int checksum = 0;
    for (int i = 0; i < 14; i++) {
        checksum += (imei[i] - '0');
    }

    imei[15] = '0' + (checksum % 10);  // 簡單校驗，實際可以使用 Luhn 算法來生成

    sqlite3_finalize(stmt);
    return imei;
}

// Luhn 校驗算法
int luhn_checksum(const char *imei) {
    int sum = 0;
    int length = strlen(imei);
    int parity = length % 2;

    for (int i = 0; i < length; i++) {
        int digit = imei[i] - '0';

        if (i % 2 == parity) {
            digit *= 2;
            if (digit > 9) {
                digit -= 9;
            }
        }

        sum += digit;
    }

    return sum % 10 == 0;
}

// 获取校验失败时的正确校验码
char get_correct_checksum(const char *imei) {
    int sum = 0;
    int length = strlen(imei);
    int parity = length % 2;
    char corrected_imei[16];

    // 将原始 IMEI 中的前14位复制到 corrected_imei 中
    strncpy(corrected_imei, imei, 14);
    corrected_imei[14] = '\0';

    for (int i = 0; i < length - 1; i++) {
        int digit = imei[i] - '0';

        if (i % 2 == parity) {
            digit *= 2;
            if (digit > 9) {
                digit -= 9;
            }
        }

        sum += digit;
    }

    // 计算正确的校验码
    int correct_digit = (10 - (sum % 10)) % 10;
    return '0' + correct_digit;
}

int validate_imei(const char *imei) {
    if (strlen(imei) != 15) {
        printf("IMEI 長度無效，應該為 15 位數字！\n");
        return 0;
    }

    // 校验 IMEI
    if (luhn_checksum(imei)) {
        return 1;
    }

    // 输出正确的校验码
    char correct_checksum = get_correct_checksum(imei);
    printf("IMEI 校驗失敗！正確的校驗碼應該是：%c\n", correct_checksum);
    return 0;
}

int main(int argc, char *argv[]) {
    sqlite3 *db;
    if (sqlite3_open(IMEI_DB, &db)) {
        fprintf(stderr, "無法打開資料庫: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // 如果是第一次使用資料庫，則創建表
    char *err_msg = NULL;
    const char *sql_create_table = "CREATE TABLE IF NOT EXISTS imei_prefix (id INTEGER PRIMARY KEY, prefix TEXT, model TEXT);";
    if (sqlite3_exec(db, sql_create_table, 0, 0, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "SQL 錯誤: %s\n", err_msg);
        sqlite3_free(err_msg);
        return 1;
    }

    int choice;
    char imei[16], model[256];
    while (1) {
        menu();
        printf("請輸入選擇: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:  // 導入前綴
                printf("請輸入CSV文件路徑: ");
                char filename[256];
                scanf("%s", filename);
                import_prefix_csv(db, filename);
                break;
            case 2:  // 生成 IMEI
                printf("請輸入設備型號: ");
                scanf("%s", model);
                char *imei_gen = generate_imei(db, model);  // 將生成的 IMEI 存入 imei_gen
                if (imei_gen) {
                    printf("生成的 IMEI: %s\n", imei_gen);
                    free(imei_gen);
                } else {
                    printf("未找到該設備型號的前綴！\n");
                }
                break;
            case 3:  // 校驗 IMEI
                printf("請輸入待校驗的IMEI: ");
                scanf("%s", imei);
                if (validate_imei(imei)) {
                    printf("IMEI 校驗成功！\n");
                } else {
                    printf("IMEI 校驗失敗！\n");
                }
                break;
            case 4:  // 退出
                sqlite3_close(db);
                return 0;
            default:
                printf("無效的選項，請重新輸入！\n");
                break;
        }
    }

    sqlite3_close(db);
    return 0;
}
