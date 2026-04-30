#include "../Utility/elo.h"

long double getExpectedScore(int playerElo, int opponentElo){
    return 1.0 / (1.0 + pow(10.0, (opponentElo - playerElo) / 400.0));
}

// score: 1 = win, 0 = loss
// kfactor : 50 for high jumps, 20 for medium jumps, 10 for low jumps
int calculateEloChange(int playerElo, int opponentElo, int score, int kFactor){
    long double expectedScore = getExpectedScore(playerElo, opponentElo);
    return (int) round(kFactor * (score - expectedScore));
}