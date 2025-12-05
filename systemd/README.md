# Systemd service для nerd-dictation-ex

## Установка

Service файл автоматически устанавливается при выполнении `make install`:

```bash
cd build
cmake ..
make install
```

Service файл будет установлен в `${CMAKE_INSTALL_PREFIX}/lib/systemd/user/nerd-dictation-ex.service`

## Использование

### Включить автозапуск

```bash
systemctl --user daemon-reload
systemctl --user enable nerd-dictation-ex
systemctl --user start nerd-dictation-ex
```

### Управление

```bash
# Запустить
systemctl --user start nerd-dictation-ex

# Остановить
systemctl --user stop nerd-dictation-ex

# Статус
systemctl --user status nerd-dictation-ex

# Логи
journalctl --user -u nerd-dictation-ex -f
```

### Отключить автозапуск

```bash
systemctl --user disable nerd-dictation-ex
```

## Конфигурация

Service файл использует переменную `@CMAKE_INSTALL_PREFIX@`, которая подставляется при сборке через `configure_file`.

Путь к исполняемому файлу в service файле:
```
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/nerd-dictation-ex daemon
```

После установки будет заменено на реальный путь, например:
```
ExecStart=/home/user/forge/nerd-dictation/install/bin/nerd-dictation-ex daemon
```

## Особенности

- **User service**: Устанавливается в `lib/systemd/user/` (не требует root)
- **Зависимости**: Требует PipeWire/PulseAudio
- **Безопасность**: Включены ограничения через systemd security features
- **Ресурсы**: Ограничение памяти до 2GB, CPU до 200%

## Ручная установка (без CMake)

Если нужно установить service файл вручную:

```bash
# Отредактировать путь в service файле
sed "s|@CMAKE_INSTALL_PREFIX@|/path/to/install|g" \
    systemd/nerd-dictation-ex.service.in > \
    ~/.config/systemd/user/nerd-dictation-ex.service

# Перезагрузить systemd
systemctl --user daemon-reload
```
