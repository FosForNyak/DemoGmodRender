# Модель RNNoise для шумодава голосу

`lq.rnnn` — модель «leavened-quisling» з https://github.com/GregorR/rnnoise-models
(каталог `leavened-quisling-2018-08-31`, SHA-256
`1957528B752799FDDF06270BC5469AF7CF54C3BADC358544AE2ABED730943FF9`). Її читає фільтр FFmpeg
`arnndn`.

За таблицею авторів це модель для сигналу «голос» (мова разом зі сміхом і вигуками) і
шуму «загальний». Серед п'яти моделей на справжніх голосах з демо GMod вона найкраще
приглушила фон, майже не зачепивши мову.

Ліцензія: за словами автора в README репозиторію, моделі не є творчою роботою і тому не
є об'єктом авторського права («none of this work is creative and thus none of it is
subject to copyright»).

Під час збірки файл копіюється поруч із програмою як `rnnoise-voice.rnnn`.
