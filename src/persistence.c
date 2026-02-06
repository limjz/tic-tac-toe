#include "game.h"

void load_scores() {
    FILE *fp = fopen("scores.txt", "r");
    if (!fp) {
        printf("No previous scores found. Starting fresh.\n");
        return;
    }
    // Simple format: Name Wins
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (fscanf(fp, "%s %d", gameData->scores[i].name, &gameData->scores[i].wins) != 2) {
            break;
        }
    }
    fclose(fp);
    printf("Scores loaded.\n");
}

void save_scores() {
    FILE *fp = fopen("scores.txt", "w");
    if (!fp) {
        perror("Failed to save scores");
        return;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        // Only save if name is set
        if (gameData->scores[i].name[0] != '\0') {
            fprintf(fp, "%s %d\n", gameData->scores[i].name, gameData->scores[i].wins);
        }
    }
    fclose(fp);
    printf("Scores saved to scores.txt.\n");
}