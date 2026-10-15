# N64 Recomp Game Template

This repository uses generic `California Speed`, `CaliforniaSpeed`, and `cspeed` placeholders so it
can be configured for another game.

Configure your game with:

```sh
python3 configure_game.py "Your Game"
```

The game name may contain only letters and spaces.

Overrides:

```sh
python3 configure_game.py "Your Game" \
  --game-id yourgame \
  --project-name YourGameRecompiled \
  --app-id io.github.username.yourgamerecompiled \
  --rom-stem yourgame.us \
  --repository https://github.com/username/YourGameRecompiled
```

Use `--preview` first to preview every file update and rename.

Search the repository for `User Task:` to find the values and
game-specific implementations that still need to be added.
