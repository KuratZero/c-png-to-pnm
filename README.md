# Преобразователь изображений (PNG to PNM)

В данном репозитории представлен проект преобразователя PNG-изображений.

Мною реализовано cli-приложение, которое, согласно стандарту, Portable Network Graphics (PNG) Specification,
преобразует PNG-изображения (сжатые без потерь алгоритмом Deflate) в формат PNM (Portable Any Map).

Преобразователь поддерживает полный стандарт PNG (PLTE и bkGd чанки, цветные, чёрно-белые изображения, а так же альфа-канал)
и предоставляет возможность выбрать один из трёх стандартных алгоритмов распаковки (ZLIB, LIBDEFLATE, ISAL).
