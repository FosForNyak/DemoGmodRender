// «Звук»: звук гри, голоси гравців із демо (таблиця з гучністю, соло, вимкненням і
// прослуховуванням), обробка голосу, субтитри «хто говорить», доріжки для монтажу, мікрофон.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Page {
    title: qsTr("Звук і голоси")
    subtitle: qsTr("Звук гри, голоси гравців із демо, обробка голосу і ваш мікрофон.")
    maxContentWidth: Theme.px(900)

    Section {
        title: qsTr("Звук у відео")
        iconName: "speaker"
        SettingToggle { key: "audio"; showAlways: true }
        SettingCombo { key: "audio_codec" }
        SettingCombo { key: "audio_bitrate" }
        SettingCombo { key: "sample_rate" }
        SettingToggle { key: "game_audio" }
        SettingSlider { key: "game_volume"; from: 0; to: 4; stepSize: 0.05; decimals: 2 }
        SettingNumber { key: "game_audio_offset"; decimals: 3; suffix: qsTr("с"); hint: qsTr("Якщо звук гри трохи відстає або випереджає картинку — підкрутіть тут. Зазвичай 0.") }
    }

    Section {
        title: qsTr("Голоси гравців")
        subtitle: qsTr("Декодовані прямо з демо — чисто і точно в часі")
        iconName: "user"
        SettingCombo { key: "voice_mode" }
        SettingSlider { key: "voice_volume"; from: 0; to: 4; stepSize: 0.05; decimals: 2 }
        SettingNumber { key: "voice_delay"; decimals: 3; suffix: qsTr("с"); hint: qsTr("Голос ставиться на час, коли пакет прийшов у демо. Додатна затримка зсуває голос пізніше.") }
        SettingToggle { key: "mute_engine_voice"; hint: qsTr("Голоси декодуються програмою прямо з демо — чисто і точно. Якщо залишити голос у грі, він потрапить у «звук гри» і може задвоїтися з декодованим.") }
        // ---- таблиця гравців ----
        Label {
            visible: !Project.loaded
            role: "secondary"
            text: qsTr("Відкрийте демо, щоб побачити гравців.")
        }
        Label {
            visible: Project.loaded && Project.players.length === 0
            role: "secondary"
            text: qsTr("Голосового чату в демо немає.")
        }
        Rectangle {
            visible: Project.players.length > 0
            Layout.fillWidth: true
            implicitHeight: playersCol.implicitHeight + 2
            radius: Theme.r2
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: playersCol
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.rowHeight
                    Layout.leftMargin: Theme.s3
                    Layout.rightMargin: Theme.s3
                    spacing: Theme.s3
                    Label { Layout.fillWidth: true; text: qsTr("Гравець"); role: "meta"; font.weight: Font.DemiBold }
                    Label { Layout.preferredWidth: Theme.px(70); text: qsTr("Говорив"); role: "meta"; font.weight: Font.DemiBold }
                    Label { Layout.preferredWidth: Theme.px(200); text: qsTr("Гучність"); role: "meta"; font.weight: Font.DemiBold }
                    Label { Layout.preferredWidth: Theme.px(118); text: ""; role: "meta" }
                }
                Repeater {
                    model: Project.players
                    Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.rowHeight + Theme.s1
                        color: index % 2 ? "transparent" : Theme.tint(Theme.surface2, 0.6)
                        readonly property bool dimmed: modelData.muted || ((Config.revision, Config.value("voice_mode")) === "selected" && !modelData.selected)
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.s3
                            anchors.rightMargin: Theme.s3
                            spacing: Theme.s3
                            Check {
                                visible: (Config.revision, Config.value("voice_mode")) === "selected"
                                checked: modelData.selected
                                onToggled: Project.setPlayerSelected(modelData.key, checked)
                                Accessible.name: qsTr("Вибрати гравця")
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name + (modelData.local ? " · " + qsTr("ви") : "")
                                    color: parent.parent.parent.dimmed ? Theme.textMuted : Theme.text
                                }
                                Label { Layout.fillWidth: true; role: "meta"; text: modelData.steamid !== "" ? modelData.steamid : modelData.key }
                            }
                            Label { Layout.preferredWidth: Theme.px(70); text: Project.formatTime(modelData.seconds); role: "secondary" }
                            Slide {
                                Layout.preferredWidth: Theme.px(200)
                                from: 0
                                to: 4
                                stepSize: 0.05
                                decimals: 2
                                value: modelData.volume
                                enabled: !Jobs.busy
                                onMoved: (v) => Project.setPlayerVolume(modelData.key, v)
                            }
                            IconBtn {
                                iconName: "headphones"
                                checked: Project.listeningKey === modelData.key
                                enabled: Project.canListen && (Project.preparingKey === "" || checked)
                                tip: checked ? qsTr("Зупинити прослуховування") : qsTr("Прослухати (15 с з початку фрагмента, з обробкою як у відео)")
                                onClicked: checked ? Project.stopListening() : Project.listen(modelData.key)
                            }
                            LetterToggle { letter: "M"; on: modelData.muted; onColor: Theme.success; tip: qsTr("Вимкнути цього гравця"); enabled: !Jobs.busy; onClicked: Project.togglePlayerMute(modelData.key) }
                            LetterToggle { letter: "S"; on: modelData.solo; onColor: Theme.warning; tip: qsTr("Лише цей гравець (соло)"); enabled: !Jobs.busy; onClicked: Project.togglePlayerSolo(modelData.key) }
                            LetterToggle { letter: "D"; on: modelData.denoise; onColor: Theme.info; tip: qsTr("Шумодав лише для цього гравця"); enabled: !Jobs.busy; onClicked: Project.togglePlayerDenoise(modelData.key) }
                        }
                    }
                }
            }
        }
    }

    Section {
        title: qsTr("Обробка голосу")
        subtitle: qsTr("Рівна гучність, шумодав, приглушення гри")
        iconName: "sparkle"
        SettingToggle { key: "voice_level"; text: qsTr("Вирівняти гучність гравців"); hint: qsTr("Кожного гравця доводимо до однакової гучності (-18 LUFS за EBU R128), виміряної по всьому його мовленню в демо: тихих стає чутно, гучні не оглушують. Підсилення постійне, без «дихання». Повзунки гучності в таблиці голосів діють поверх цього.") }
        SettingToggle { key: "voice_denoise"; text: qsTr("Шумодав для всіх гравців"); hint: qsTr("Нейромережевий шумодав RNNoise (фільтр arnndn) прибирає фон — шипіння, клавіатуру, звук гри з колонок — навіть коли він майже такий гучний, як мова. Потім гейт глушить паузи між фразами; його поріг рахується для кожного гравця з його ж мовлення, тож тихі гравці не обрізаються.\nЛише для окремих гравців — правий клік на імені в таблиці голосів.") }
        SettingToggle { key: "duck_game"; text: qsTr("Приглушувати звук гри, коли хтось говорить"); hint: qsTr("Постріли й музика стихають, поки звучить голос, і плавно повертаються після фрази (фільтр sidechaincompress). Окрема доріжка гри для монтажу лишається без змін.") }
        SettingCombo { key: "loudness_target"; label: qsTr("Гучність результату"); hint: qsTr("Загальний мікс доводиться до цієї гучності за EBU R128 (фільтр loudnorm), з обмеженням піків -1.5 dBTP. YouTube і стрімінгові сервіси самі приглушують гучніше -14 LUFS.") }
    }

    Section {
        title: qsTr("Субтитри й підписи")
        subtitle: qsTr("Хто говорить — у файлі .srt і прямо на кадрі")
        iconName: "chat"
        SettingToggle { key: "subtitles_srt"; hint: qsTr("Файл .srt з іменами гравців у моменти, коли вони говорять. VLC і mpv підхоплюють його самі, а в програмі монтажу за ним легко знайти потрібні репліки.") }
        SettingToggle { key: "speech_subtitles"; hint: qsTr("Замість самих імен — що саме гравці кажуть: «Ім'я: текст». Мовлення розпізнається локально (whisper.cpp) перед рендером, лише для фрагмента; уже розпізнане (сторінка «Чат і мовлення») береться готовим.") }
        SettingToggle { key: "speaker_overlay"; text: qsTr("Підписи «хто говорить» прямо на відео"); hint: qsTr("Поки гравець говорить, праворуч унизу кадру видно плашку з його ніком — як індикатор голосового чату в самій грі. Зручно, коли HUD приховано або голос гри вимкнено. Потрапляє в усі версії відео.") }
        SettingToggle { key: "chat_srt"; hint: qsTr("Поруч із відео — .srt з повідомленнями чату з фрагмента (кожне видно 7 с).\nКорисно, якщо HUD приховано. Разом із субтитрами «хто говорить» — файл .chat.srt.") }
        SettingCombo { key: "whisper_language" }
    }

    Section {
        title: qsTr("Для монтажу")
        subtitle: qsTr("Окремі доріжки і проєкт для Premiere Pro / DaVinci Resolve")
        iconName: "film"
        SettingToggle { key: "separate_tracks"; text: qsTr("Окремі звукові доріжки у файлі"); hint: qsTr("Крім загального міксу, у файл буде записано окремі доріжки: гра, кожен гравець, мікрофон. Зручно для Premiere/DaVinci Resolve.") }
        SettingToggle { key: "edit_package"; hint: qsTr("Поруч із відео з'явиться тека: окремі WAV гри, кожного гравця і мікрофона (24 біт, рівно від першого кадру) і проєкт XML. Premiere Pro і DaVinci Resolve відкривають його через File → Import: відео і всі доріжки одразу на шкалі, позначки — маркерами.") }
        RowLayout {
            spacing: Theme.s2
            Btn {
                text: qsTr("Зберегти голоси в теку…")
                iconName: "download"
                enabled: Project.players.length > 0 && !Jobs.busy
                onClicked: voicesDialog.open()
            }
            Label { role: "meta"; text: qsTr("кожен гравець — окремий WAV (або FLAC) на весь фрагмент") }
        }
    }

    Section {
        title: qsTr("Власний мікрофон")
        subtitle: qsTr("Окремий запис вашого голосу (OBS, Audacity, Discord)")
        iconName: "mic"
        SettingPath {
            key: "mic_file"
            mode: "open"
            showAlways: true
            nameFilters: [qsTr("Аудіо (*.wav *.mp3 *.ogg *.flac *.m4a *.opus)"), qsTr("Усі файли (*)")]
            placeholder: qsTr("не вибрано — або перетягніть аудіофайл у вікно")
        }
        SettingNumber { key: "mic_offset"; decimals: 3; suffix: qsTr("с"); showAlways: true; hint: qsTr("Час у відео, де починається файл мікрофона. Від'ємне значення обрізає початок файлу.") }
        SettingSlider { key: "mic_volume"; from: 0; to: 4; stepSize: 0.05; decimals: 2; showAlways: true }
    }
    Item { Layout.preferredHeight: Theme.s6 }

    FolderDialogWrap { id: voicesDialog; title: qsTr("Куди зберегти голоси"); onChosen: (p) => Jobs.exportVoices(p) }
}
