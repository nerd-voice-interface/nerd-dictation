#!/bin/bash
# Нормализация звуков nerd-dictation до 0 dB (peak)
#
# Использование: ./normalize-sounds.sh [target_dB]
# По умолчанию нормализует до -1 dB (чтобы избежать клиппинга)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOUNDS_DIR="$SCRIPT_DIR/../sounds"
TARGET_DB="${1:--1}"  # По умолчанию -1 dB

echo "=== Нормализация звуков nerd-dictation ==="
echo "Целевой уровень: ${TARGET_DB} dB"
echo

for wav in "$SOUNDS_DIR"/*.wav; do
    if [[ ! -f "$wav" ]]; then
        continue
    fi

    filename=$(basename "$wav")
    echo "Обработка: $filename"

    # Получаем текущий max_volume
    max_vol=$(ffmpeg -i "$wav" -af "volumedetect" -f null /dev/null 2>&1 | \
              grep "max_volume" | sed 's/.*max_volume: //' | sed 's/ dB//')

    if [[ -z "$max_vol" ]]; then
        echo "  Ошибка: не удалось определить громкость"
        continue
    fi

    echo "  Текущий max: ${max_vol} dB"

    # Вычисляем необходимое усиление
    # gain = target - current
    gain=$(awk "BEGIN {printf \"%.1f\", $TARGET_DB - ($max_vol)}")

    echo "  Усиление: ${gain} dB"

    # Создаём резервную копию
    backup="${wav}.backup"
    if [[ ! -f "$backup" ]]; then
        cp "$wav" "$backup"
        echo "  Резервная копия: $backup"
    fi

    # Нормализуем
    temp="${wav}.tmp.wav"
    ffmpeg -y -i "$wav" -af "volume=${gain}dB" -c:a pcm_s16le "$temp" 2>/dev/null
    mv "$temp" "$wav"

    # Проверяем результат
    new_max=$(ffmpeg -i "$wav" -af "volumedetect" -f null /dev/null 2>&1 | \
              grep "max_volume" | sed 's/.*max_volume: //' | sed 's/ dB//')
    echo "  Новый max: ${new_max} dB"
    echo
done

echo "=== Готово ==="
echo "Для отката используйте файлы *.backup"
