# BerryTrip Launcher

BerryTrip Launcher — неофициальный форк [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher),
собранный для одного сервера BerryTrip (Minecraft 1.21.1, NeoForge 21.1.267).

## Лицензия

Программа распространяется по **GNU General Public License v3.0** (файл `LICENSE` в этой папке).
Исходный код этой сборки доступен по адресу:

- репозиторий: https://github.com/evilvalue/prism-berry
- ветка: `berry`
- тег, соответствующий выпуску: см. страницу релиза на GitHub

Каждый получатель бинарника имеет право запросить и получить полный исходный код этой версии
по указанному адресу — это требование GPL v3 выполнено: код публичен и совпадает с собранным.

## Авторские права

- © 2026 BerryTrip
- © 2022-2026 Prism Launcher Contributors
- © 2021-2022 PolyMC Contributors
- © 2012-2021 MultiMC Contributors

Prism Launcher, PolyMC и MultiMC распространяются по GPL v3; их код используется на условиях этой лицензии.
Список изменений форка перечислен в истории коммитов ветки `berry` и в описании релиза.

## Изменения относительно Prism Launcher

Под опцией сборки `BERRY_SOLO=ON` добавлено:

- пиксельный интерфейс BerryTrip (палитра, шрифт Monocraft, пять экранов) поверх `MainWindow`;
- вход только через Telegram-бота (JWT-токен в `config/berryauth-token.json`), оффлайн-аккаунт Prism;
- автосоздание и автообновление единственного инстанса BerryTrip из HTTPS-манифеста с SHA-256/SHA-512;
- управление необязательными модами, ресурспаками, шейдерами, памятью и Java;
- сброс конфигов с сохранением токена входа;
- диагностика и проверка обновлений.

Остальной код — Prism Launcher без изменений, кроме двух правок совместимости с Qt 6.8.3/MSVC
(`launcher/DesktopServices.cpp`, `launcher/archive/ArchiveWriter.cpp`).

## Сборка из исходников

```bat
packaging\configure.bat -DBERRY_SOLO=ON
packaging\build.bat
packaging\package.bat
packaging\berrytrip.nsi   :: установщик, нужен makensis
```

## Товарные знаки

Minecraft — торговая марка Mojang Studios. Проект не связан с Mojang Studios и Microsoft
и не одобрен ими. Prism Launcher — проект сообщества, не связанный с Mojang Studios.
