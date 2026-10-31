# dist/ — готовые сборки

Собранные программы **не хранятся в репозитории**: они публикуются в
[**Releases**](https://github.com/klimov-kn/TMC_Suite_MacOS/releases).

Как собрать самому — в [`BUILD_ORDER.md`](../BUILD_ORDER.md), подробно — на сайте проекта
[www.tamic.ru](https://www.tamic.ru/docs/platforms/macos.html#build). Результаты появятся здесь:

```
dist/
├── macos/
│   ├── PlanarRT_H.app, PlanarRT_X.app, TMCGROUT.app, TMC_DN.app, TMCROS.app, FieldView.app, TplDesigner.app
│   └── lib/    библиотеки пакета
├── TMC_Suite-2.0.0-macos-universal.dmg      образ (собирает installer/package_macos.sh)
└── TMC_Suite-2.0.0-macos-universal.zip      архив
```
