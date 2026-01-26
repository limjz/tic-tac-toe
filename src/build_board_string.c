#include "game.h"

void build_board_string(char *out, size_t out_sz) {
    size_t used = 0;
    used += (size_t)snprintf(out + used, out_sz - used, "\n    0 1 2 3\n");
    for (int r = 0; r < 4; r++) {
        used += (size_t)snprintf(out + used, out_sz - used, "%d | ", r);
        for (int c = 0; c < 4; c++) {
            char cell = gameData->boardGame[r][c];
            if (cell == 0) cell = '.';
            used += (size_t)snprintf(out + used, out_sz - used, "%c ", cell);
        }
        used += (size_t)snprintf(out + used, out_sz - used, "\n");
        if (used >= out_sz) break;
    }
    used += (size_t)snprintf(out + used, out_sz - used, "\n");
}