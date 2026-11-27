# Ложные срабатывания: что реально можно сделать

VirusTotal **не снимает детекции сам** — он только показывает вердикты движков.
В поддержке VT прямо отвечают: обращайтесь к вендору, чей движок сработал; VT может
поправить только кэш URL. Поэтому «заявка в VirusTotal» на практике — это заявки
в каждый антивирус, который пометил файл.

## 1. Загрузить файл в VT

```powershell
# бесплатный ключ: https://www.virustotal.com/gui/user/apikey
# положить в prism-berry\.vtkey (файл в .gitignore) или задать $env:VT_API_KEY
powershell -ExecutionPolicy Bypass -File packaging\submit_vt.ps1
```

Скрипт выкладывает установщик и `berrytrip.exe`, ждёт анализа и печатает список
движков, которые пометили файл. Отчёт сохраняется в `logs\vt-report.json`,
ссылки вида `https://www.virustotal.com/gui/file/<sha256>`.

## 2. Что считается нормой для такого проекта

- NSIS-установщик без подписи CA и без репутации часто ловит 1–5 эвристических
  детекций (`Wacatac`, `InstallCore`, `Gen:Variant`, `Unsafe`, `PUA`).
- Самоподписанный сертификат детекции **не уменьшает**: он не даёт репутации.
- Детекции вида `Trojan:Win32/Wacatac.B!ml` у ML-движков (Microsoft, CrowdStrike,
  Trapmine) на новых бинарниках — типичное ложное срабатывание, но снимать его
  должен вендор.

## 3. Письмо вендору (шаблон)

> Subject: False positive report — BerryTrip Launcher 0.3.1 (GPL-3.0, open source)
>
> Hello,
>
> Your engine flags our open-source launcher as malicious. The file is built from
> public sources and is not packed or obfuscated.
>
> - Product: BerryTrip Launcher 0.3.1 (fork of Prism Launcher, GPL-3.0)
> - Source code: https://github.com/evilvalue/prism-berry/tree/v0.3.1
> - Release: https://github.com/evilvalue/prism-berry/releases/tag/v0.3.1
> - SHA-256: <из отчёта VT>
> - VirusTotal report: https://www.virustotal.com/gui/file/<sha256>
> - Installer: NSIS, per-user install to %LOCALAPPDATA%\Programs\BerryTrip,
>   no admin rights, no downloads, no persistence beyond shortcuts and an
>   uninstall entry in HKCU.
> - The launcher opens a Minecraft server, checks an HTTPS pack manifest and signs
>   in through a Telegram bot; it does not touch system files.
>
> Please review and remove the detection.
>
> <имя>, BerryTrip

## 4. Куда отправлять

| Движок | Куда |
|---|---|
| Microsoft Defender | https://www.microsoft.com/en-us/wdsi/filesubmission (выбрать «Software developer» → false positive) |
| Google (GoogleSafeBrowsing) | не для файлов |
| CrowdStrike | VTscanner@crowdstrike.com |
| Fortinet | https://www.fortiguard.com/faq/onsite |
| Kaspersky | https://opentip.kaspersky.com/ → Report false positive |
| Bitdefender | https://www.bitdefender.com/consumer/support/answer/29358/ |
| ESET | samples@eset.com |
| Avast / AVG | https://www.avast.com/false-positive-file-form.php |
| Dr.Web | vms@drweb.com |
| Sophos | https://support.sophos.com (Sample submission) |
| Ikarus | support@ikarus.at |
| Trapmine | support@trapmine.com |
| MaxSecure | support@maxsecureantivirus.com |
| Elastic | https://www.elastic.co/community/security (false positive) |

Полный список контактов движков VT: https://docs.virustotal.com/docs/false-positive-contacts

## 5. Что уменьшает срабатывания в будущем

1. Подписывать **все** релизы одним издателем (репутация накапливается).
2. Не менять установщик без необходимости и не использовать UPX/самораспаковку.
3. Отправлять FP на каждый новый релиз, а не один раз.
4. Для открытого проекта — бесплатная подпись через SignPath Foundation
   (издатель будет «SignPath Foundation», зато сертификат доверенный).
