# Vendoring external/

Four components are vendored by hand (not fetched via CMake FetchContent): `glad/` (generated), `stb/stb_image.h`, `miniaudio/miniaudio.h`, `tomlplusplus/toml.hpp`. The first three are per the locked architecture; toml++ was added with the game manifest, by the same rule — a single header with its licence inside it is cheaper to vendor than to fetch. Already fetched and committed — this file documents how, for re-vendoring later (e.g. bumping stb_image, regenerating glad for a different GL API/extension set).

## stb_image.h

```bash
curl -L -o external/stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

## toml.hpp

Single-header build of toml++ (currently v3.4.0, MIT, licence text at the top of
the file).

```bash
curl -L -o external/tomlplusplus/toml.hpp https://raw.githubusercontent.com/marzer/tomlplusplus/master/toml.hpp
```

## miniaudio.h

```bash
curl -L -o external/miniaudio/miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
```

## glad (OpenGL 3.3 Core loader)

Must land at exactly these paths (matches `external/glad/CMakeLists.txt`):

- `external/glad/src/glad.c`
- `external/glad/include/glad/glad.h`
- `external/glad/include/KHR/khrplatform.h`

Option A — glad Python generator (used for the current vendored copy, glad 0.1.36, no extensions):

```bash
python3 -m venv /tmp/glad-env && /tmp/glad-env/bin/pip install glad
/tmp/glad-env/bin/python -m glad --generator=c --spec=gl --profile=core --api="gl=3.3" --out-path=external/glad
rm -rf /tmp/glad-env
```

Option B — web generator (if the above fails or you'd rather not pip-install anything):

1. Open https://glad.dav1d.de/
2. Language: C/C++, API gl = 3.3, Profile: Core, Generator: C, no extensions needed for now.
3. Generate → download zip → extract so `glad.c`→`external/glad/src/glad.c`, `glad.h`→`external/glad/include/glad/glad.h`, `khrplatform.h`→`external/glad/include/KHR/khrplatform.h`.
