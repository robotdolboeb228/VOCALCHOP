# Vocal Chop — VST3 / AU эффект (macOS)

Вешаешь на дорожку с вокалом. Плагин пишет вход в буфер и ретриггерит кусок:
по сетке хоста (1/4 … 1/32, триоли, swing, вероятность) и/или по MIDI-нотам
(C3 = оригинальная высота, каждая клавиша = +1 полутон).

## Интерфейс
Свой дизайн (тёмный, неон magenta → cyan), окно можно тянуть за угол, пропорции сохраняются.
- 16 пэдов — маска шагов для режима Grid (выключенный шаг пропускается), подсвечивается текущий шаг
- Волна — последние 2 секунды входа; рамка показывает, какой кусок сейчас ретриггерится
- Метр OUTPUT — пик выхода

## Параметры
- **Mode** — Grid / MIDI / Grid + MIDI
- **Rate, Swing, Probability** — для сетки
- **Length (ms)** или **Length = Step** — длина куска
- **Offset** — на сколько мс назад от «сейчас» брать кусок
- **Pitch, Reverse, Attack, Release, Dry, Wet**

## Сборка без установки компилятора
1. Создай репозиторий на GitHub и залей туда содержимое папки.
2. Вкладка **Actions** → Build (macOS) → дождись зелёной галочки.
3. В запуске скачай артефакт `VocalChop-macOS` (внутри zip).

## Установка
```
cp -R "Vocal Chop.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "Vocal Chop.component" ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Vocal Chop.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Vocal Chop.component"
```
Перезапусти DAW и пересканируй плагины. AU — для Logic/GarageBand, VST3 — для Ableton, FL, Reaper и др.

## Локальная сборка
```
brew install cmake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## MIDI в DAW
Плагин-эффект должен получать MIDI. Ableton: нужна MIDI-дорожка со ссылкой на аудио-дорожку
через sidechain/«MIDI To» не подходит для эффектов — проще Reaper/FL/Logic; в Ableton используй
MIDI-дорожку → Audio Effect Rack. Режим «Grid» работает везде без MIDI.
