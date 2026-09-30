#include "thor/enemy_engine.hpp"
#include "thor/enemy_projectiles.hpp"

namespace thor {
// Every ported area adds one line here: `register_zoomer(game);` with the declaration above.
void Game::register_all(Game& game) {
    enemy::register_engine(game);
    enemy::register_projectiles86(game);
}
}
