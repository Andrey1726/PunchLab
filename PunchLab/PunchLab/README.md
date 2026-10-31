# PunchLab — многомодульный процессор ударных (VST3)

Аудиоэффект для формирования тембра, усиления транзиентов, управления динамикой,
пространственной обработки и повышения плотности ударных и перкуссии.
Работает на отдельных дорожках, барабанных шинах, драм-машинах и лупах.
Написан на C++17 / JUCE 8, формат VST3 (Windows, macOS, Linux), на macOS также AU,
плюс Standalone-приложение для быстрой проверки.

## Сборка

JUCE 8.0.9 скачивается автоматически архивом при первой конфигурации (Git не нужен).
Кросс-компиляция под Windows из Linux в JUCE 8 не поддерживается, поэтому Windows-версию
собирают либо на самом Windows, либо на серверах GitHub.

### Windows: вариант 1 — без установки чего-либо (GitHub Actions)

1. Создайте репозиторий на github.com (можно приватный) и загрузите в него содержимое
   этой папки (кнопка «Add file → Upload files», включая скрытую папку `.github`).
2. Откройте вкладку **Actions** — сборка запустится сама (≈ 10 минут).
3. В завершённом запуске внизу, в разделе **Artifacts**, скачайте `PunchLab-Windows-x64-VST3`.
4. Распакуйте и скопируйте папку `PunchLab.vst3` в `C:\Program Files\Common Files\VST3\`.

### Windows: вариант 2 — локально одним файлом

1. Установите бесплатную Visual Studio Community 2022 или 2026 с набором
   «Разработка классических приложений на C++» (CMake входит в набор).
2. Запустите `build_windows.bat` от имени администратора. Скрипт сам найдёт
   Visual Studio, скачает JUCE, соберёт плагин и скопирует его в папку VST3.

Вручную то же самое (в «Developer PowerShell for VS»):

```
cmake -B build -A x64
cmake --build build --config Release --target PunchLab_VST3
```

Плагин появится в `build\PunchLab_artefacts\Release\VST3\PunchLab.vst3`.

### macOS (Xcode)

```
cmake -B build -G Xcode
cmake --build build --config Release --target PunchLab_VST3
```

Скопируйте `build/PunchLab_artefacts/Release/VST3/PunchLab.vst3` в
`~/Library/Audio/Plug-Ins/VST3/`. Сборка без подписи: при первом запуске может
понадобиться `xattr -cr ~/Library/Audio/Plug-Ins/VST3/PunchLab.vst3`.

### Linux

```
sudo apt install cmake ninja-build libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PunchLab_VST3
```

### В Ableton Live

Preferences → Plug-Ins → включите «Use VST3 Plug-In System Folders» → Rescan.
Плагин появится в браузере: Plug-Ins → PunchLab Audio → PunchLab.
Латентность (≈ 1146 сэмплов при 48 кГц) сообщается хосту и компенсируется Live
автоматически.

### Офлайн-тест DSP

```
cmake -B build -DPUNCHLAB_BUILD_TESTS=ON
cmake --build build --target PunchLabTest
./build/PunchLabTest_artefacts/Release/PunchLabTest
```

Тест рендерит синтетический барабанный луп и проверяет: побитовую прозрачность при
выключенных модулях (с учётом латентности), прозрачность фазового вокодера при нулевом
сдвиге, точную сумму «транзиент + сустейн», отсутствие NaN/Inf во всех модулях,
режимах и 33 заводских пресетах, сохранение состояния, работу на 44.1/96 кГц и
блоках больше заявленного размера.

## Устройство

```
вход ─┬─► [слот 1] ─► [слот 2] ─► … ─► [слот 7] ─► Master ─► Mix ─► Output ─► Loudness Match ─► Bypass ─► выход
      └──────────────────── сухой сигнал (задержан на латентность цепочки) ────────┘
```

Каждый слот:

```
x ─► Focus (LR4-кроссовер) ─┬─ низ+верх ───────────────────────────────┐
                            └─ полоса ─► Target (t = g·x, s = (1−g)·x) ─┤
                                         обрабатываемая часть ─► МОДУЛЬ ─┴─► сумма ─► плавное вкл/выкл
```

Итоговая интенсивность модуля = Amount × Magnitude. Magnitude также масштабирует
все отклонения мастер-секции от нейтрали (EQ, Air, Attack/Sustain, Clip).

| Модуль | Алгоритм | Параметры |
|---|---|---|
| Boom Shack | Два детектора ударов (ФНЧ 150 Гц и ФВЧ 1 кГц). НЧ-удар запускает синус с питч-свипом (как у 808), средне-высокий — всплеск полосового шума. Громкость повторяет силу удара | Sub, Tune, Decay, Noise, Noise Tone, Noise Decay, Sensitivity |
| Shift | Фазовый вокодер (Ханн, 75 % перекрытия, перенос бинов) + коррекция спектральной огибающей для формант + частотный сдвиг (SSB через IIR-преобразователь Гильберта) | Pitch ±24 пт, Formant ±12 пт (0 = форманты следуют за тоном; Formant = −Pitch сохраняет их), Freq ±1 кГц |
| Space | FDN: 8 линий, матрица Хаусхолдера, 4 all-pass диффузора, частотно-зависимое затухание, модуляция линий, M/S-ширина | Type (Room, Plate, Hall, Gated, Ambience), Size, Decay, Pre-Delay, Damping, Width |
| Compress | Feed-forward, стерео-связанный детектор, мягкое колено, адаптивная компенсация | Mode (Glue, Punch, Smash, Pump, Opto, Limit), Speed, Mix |
| Saturate | 4× передискретизация (FIR, целочисленная латентность) | Mode (Tape, Tube, Transistor, Fuzz, Fold, Crush), Drive, Tone |
| Experimental | Кольцевой буфер 10 с, сетка от темпа и позиции Live | Mode (Granular, Stutter, Reverse, Tape Stop, Scatter, Ring Mod), Grid, Size, Chaos, Pitch |
| More | 4× передискретизация, морфинг «мягкий вейвшейпер → клиппер», потолок | Character, Ceiling |

**Target**: разность быстрой и медленной огибающих даёт коэффициент g ∈ [0, 1];
транзиентная часть g·x и сустейновая (1−g)·x в сумме точно равны исходнику, поэтому
разделение не вносит фазовых искажений. Глобальные ручки Length и Sensitivity
(в правой колонке панели модуля) задают длительность и чувствительность.

**Master**: Low shelf 100 Гц, Mid peak (150 Гц–8 кГц), High shelf 6 кГц, Air shelf 14 кГц,
транзиент-шейпер Attack/Sustain (±12 дБ), мягкий клиппер, Mix, Output.

**Loudness Match**: K-взвешенная мощность входа и выхода (окно ≈ 1.5 с), плавный
компенсирующий гейн ±24 дБ, в тишине «замораживается». Стоит последним, поэтому при
включённом LM ручка Output не меняет громкость — это сделано для честного A/B с Bypass.

## Интерфейс

* Верхняя панель: браузер пресетов (‹ ›, меню по категориям, Save, Delete),
  Loudness Match, Bypass, Magnitude.
* Цепочка: 7 плиток в порядке обработки. Перетащите плитку на другую, чтобы
  поменять порядок. На плитке: питание, замок (модуль не меняется при загрузке
  пресета — ни параметры, ни позиция), Amount, метки TRN/SUS/FOC.
* Клик по плитке открывает панель модуля: его параметры, Target, Focus с визуализацией
  полосы, индикатор подавления для Compress.
* Окно масштабируется (60–160 %) за правый нижний угол.
* Двойной клик по ручке — значение по умолчанию.

Пользовательские пресеты: `Документы/PunchLab/Presets/*.plpreset` (XML).
Заводские пресеты (33) доступны также через список программ хоста.

## Структура исходников

```
Source/
  Params.*            параметры и их диапазоны
  PluginProcessor.*   цепочка, мастер, dry/wet, LM, bypass, состояние
  PluginEditor.*      окно плагина и панели модулей
  PresetManager.*     загрузка/сохранение, блокировка модулей
  FactoryPresets.cpp  заводские пресеты
  dsp/ModuleSlot.h    интерфейс модуля, Target, Focus, компенсация латентности
  dsp/*.h             модули, мастер-секция, Loudness Match
  gui/Widgets.*       LookAndFeel, ручки, плитки, индикаторы
Tests/SmokeTest.cpp   офлайн-тест
```

## Лицензия

JUCE распространяется под AGPLv3 или коммерческой лицензией. Для личного и учебного
использования ограничений нет; для распространения плагина ознакомьтесь с условиями
лицензии JUCE.
