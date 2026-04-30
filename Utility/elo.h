#ifndef ELO_H
#define ELO_H

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../Utility/elo.h"
#include "../Utility/structs.h"
#include "../Utility/codes.h"
#include "../Server/queryDatabase.h"
#include "../Server/genericDatabase.h"

long double getExpectedScore(int playerElo, int opponentElo);

// score: 1 = win, 0 = loss
// kfactor : 50 for high jumps, 20 for medium jumps, 10 for low jumps
int calculateEloChange(int playerElo, int opponentElo, int score, int kFactor);

int updateDuelElos(char *winner, char *loser);

#endif