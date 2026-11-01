# xylonic-fiber
Xylonic Fiber: performance-focused ground-up rewrite of the Xylonic Subsonic client, low CPU and RAM use

## Linux build

From the project root, run:

```sh
./build-linux.sh
```

This configures a Release build in `build-linux/`, builds the native executable, runs the CTest suite, and prints the executable path. To build, test, and launch it:

```sh
./build-linux.sh --run
```

Pass normal application arguments after `--run`, for example:

```sh
./build-linux.sh --run --server https://music.example --user me --pass secret
```

Use `XF_BUILD_DIR` to choose another build directory and `XF_BUILD_JOBS` to control parallelism.
