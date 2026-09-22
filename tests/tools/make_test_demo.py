#!/usr/bin/env python3
"""
make_test_demo.py — генерує СИНТЕТИЧНИЙ .dem файл у форматі Garry's Mod
(HL2DEMO, demo protocol 3, network protocol 24) для тестування парсера.

Містить: svc_ServerInfo (формат GMod), таблицю userinfo (2 гравці + бот,
третій гравець додається пізніше через svc_UpdateStringTable), svc_VoiceInit,
багато "шумових" повідомлень (PacketEntities, TempEntities, net.* GMod ...)
і справжній голос у форматі Steam Voice (Opus 24 кГц, кадри по 20 мс).

Використання:  python3 make_test_demo.py out.dem truth.json [--pe-bits 24] [--compress-userinfo] [--legacy] [--gmod2026]

Чат: user message SayText (тип 3), TextMsg (тип 4, у чат) і ігрові події
(svc_GameEventList + player_connect_client / player_disconnect / player_say).

--gmod2026: формат серверів GMod 2026 року (білд 10000+): svc_ServerInfo має 16 біт
у кінці, а svc_CreateStringTable передає розмір таблиці як log2 у 5 бітах.
Потрібен ffmpeg з libopus (для кодування тестового голосу).
"""
import json, math, os, random, struct, subprocess, sys, tempfile, zlib

TICK_INTERVAL = 1.0 / 66.0
EDICT_BITS = 13


class BitWriter:
    def __init__(self):
        self.buf = bytearray()
        self.acc = 0
        self.nacc = 0

    def bits(self, value, n):
        if n == 0:
            return
        value &= (1 << n) - 1
        self.acc |= value << self.nacc
        self.nacc += n
        while self.nacc >= 8:
            self.buf.append(self.acc & 0xFF)
            self.acc >>= 8
            self.nacc -= 8

    def bit(self, b): self.bits(1 if b else 0, 1)
    def byte(self, v): self.bits(v, 8)
    def word(self, v): self.bits(v, 16)
    def long(self, v): self.bits(v & 0xFFFFFFFF, 32)
    def float(self, f): self.bits(struct.unpack('<I', struct.pack('<f', f))[0], 32)

    def string(self, s):
        for b in s.encode('utf-8'):
            self.byte(b)
        self.byte(0)

    def raw(self, data):
        if self.nacc == 0:
            self.buf.extend(data)
        else:
            for b in data:
                self.byte(b)

    def varint32(self, v):
        while True:
            b = v & 0x7F
            v >>= 7
            if v:
                self.byte(b | 0x80)
            else:
                self.byte(b)
                break

    def random_bits(self, n, rnd):
        full = n // 8
        self.raw(bytes(rnd.getrandbits(8) for _ in range(full)))
        if n % 8:
            self.bits(rnd.getrandbits(n % 8), n % 8)

    def num_bits(self): return len(self.buf) * 8 + self.nacc

    def getvalue(self):
        out = bytearray(self.buf)
        if self.nacc:
            out.append(self.acc & 0xFF)
        return bytes(out)


# Ігрові події, як у справжньому GMod (номер, назва, поля: 1 рядок, 4 short, 5 byte, 6 bool)
GAME_EVENTS = [
    (8, 'player_connect_client', [('name', 1), ('index', 5), ('userid', 4), ('networkid', 1), ('bot', 4)]),
    (10, 'player_disconnect', [('userid', 4), ('reason', 1), ('name', 1), ('networkid', 1), ('bot', 4)]),
    (12, 'player_say', [('userid', 4), ('text', 1), ('teamonly', 6)]),
    (29, 'player_spawn', [('userid', 4)]),
]


def game_event_list(w):
    body = BitWriter()
    for eid, name, keys in GAME_EVENTS:
        body.bits(eid, 9); body.string(name)
        for k, t in keys:
            body.bits(t, 3); body.string(k)
        body.bits(0, 3)
    msg_type(w, 30); w.bits(len(GAME_EVENTS), 9); w.bits(body.num_bits(), 20)
    copy_bits(w, body)


def game_event(w, name, values):
    eid, _, keys = next(e for e in GAME_EVENTS if e[1] == name)
    body = BitWriter()
    body.bits(eid, 9)
    for k, t in keys:
        v = values[k]
        if t == 1: body.string(v)
        elif t == 4: body.word(v & 0xFFFF)
        elif t == 5: body.byte(v)
        elif t == 6: body.bit(v)
    msg_type(w, 25); w.bits(body.num_bits(), 11)
    copy_bits(w, body)


def user_message(w, um_type, payload):
    msg_type(w, 23); w.byte(um_type); w.bits(len(payload) * 8, 11); w.raw(payload)


def say_text(entity, text):
    return bytes([entity]) + text.encode('utf-8') + b'\x00' + b'\x01\x00\x00'


def text_msg(dest, text):
    return bytes([dest]) + text.encode('utf-8') + b'\x00' * 5


def copy_bits(w, src):
    data = src.getvalue()
    for i in range(src.num_bits()):
        w.bit((data[i >> 3] >> (i & 7)) & 1)


def q_log2(v):
    a = 0
    while v > 1:
        v >>= 1
        a += 1
    return a


def msg_type(w, t): w.bits(t, 6)


GMOD2026 = '--gmod2026' in sys.argv


def table_max_entries(w, max_entries):
    # Розмір таблиці у svc_CreateStringTable: 16 біт, а в GMod 2026 — log2 у 5 бітах
    if GMOD2026:
        w.bits(q_log2(max_entries), 5)
    else:
        w.word(max_entries)


# Сучасний Garry's Mod (за замовчуванням): підпис GMODEMO, довжина user data у
# таблицях рядків — 19 біт, player_info_t — 324 байти (ім'я до 128 байт).
# --legacy: класичний Source 2013 (HL2DEMO, 14 біт, 132 байти).
MODERN = '--legacy' not in sys.argv
UD_LEN_BITS = 19 if MODERN else 14


# ---- player_info_t -------------------------------------------------------------
def player_info(name, userid, guid, friends_id, fake=False):
    if MODERN:
        b = bytearray(324)
        nb = name.encode('utf-8')[:127]
        b[0:len(nb)] = nb
        struct.pack_into('<i', b, 128, userid)
        gb = guid.encode()[:32]
        b[132:132 + len(gb)] = gb
        struct.pack_into('<I', b, 168, friends_id)
        b[172:172 + len(nb)] = nb
        b[300] = 1 if fake else 0
        return bytes(b)
    b = bytearray(132)
    nb = name.encode('utf-8')[:31]
    b[0:len(nb)] = nb
    struct.pack_into('<i', b, 32, userid)
    gb = guid.encode()[:32]
    b[36:36 + len(gb)] = gb
    struct.pack_into('<I', b, 72, friends_id)
    b[76:76 + len(nb)] = nb
    b[108] = 1 if fake else 0
    return bytes(b)


def lzss_literal_only(data):
    """Найпростіше "стискання" LZSS (лише літерали + маркер кінця) — щоб перевірити розпаковку."""
    out = bytearray(b'LZSS') + struct.pack('<I', len(data))
    items = [bytes([b]) for b in data] + [None]   # None = маркер кінця
    for i in range(0, len(items), 8):
        group = items[i:i + 8]
        cmd = 0
        for k, it in enumerate(group):
            if it is None:
                cmd |= 1 << k
        out.append(cmd)
        for it in group:
            out.extend(b'\x00\x00' if it is None else it)
    return bytes(out)


def userinfo_table_data(entries, max_entries):
    """entries: list of (index, key, userdata)"""
    w = BitWriter()
    last = -1
    ebits = q_log2(max_entries)
    for idx, key, ud in entries:
        if idx == last + 1:
            w.bit(1)
        else:
            w.bit(0)
            w.bits(idx, ebits)
        last = idx
        w.bit(1)          # є рядок
        w.bit(0)          # без підрядка з історії
        w.string(key)
        w.bit(1)          # є userdata
        w.bits(len(ud), UD_LEN_BITS)
        w.raw(ud)
    return w


# ---- Steam Voice -------------------------------------------------------------
def encode_opus_frames(freq, seconds, amp=0.4):
    """Кодує синусоїду в Opus (24 кГц моно, кадри 20 мс) і повертає список пакетів."""
    with tempfile.TemporaryDirectory() as td:
        raw = os.path.join(td, 'in.raw')
        ogg = os.path.join(td, 'out.ogg')
        n = int(seconds * 24000)
        samples = bytearray()
        for i in range(n):
            # "мовлення": синус з повільною амплітудною модуляцією
            env = 0.6 + 0.4 * math.sin(2 * math.pi * 3 * i / 24000)
            v = int(32767 * amp * env * math.sin(2 * math.pi * freq * i / 24000))
            samples += struct.pack('<h', v)
        open(raw, 'wb').write(samples)
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 's16le', '-ar', '24000',
                        '-ac', '1', '-i', raw, '-c:a', 'libopus', '-b:a', '32k', '-frame_duration', '20',
                        '-application', 'voip', ogg], check=True)
        return parse_ogg_packets(open(ogg, 'rb').read())[2:]  # пропускаємо OpusHead і OpusTags


def parse_ogg_packets(data):
    packets, cur, pos = [], bytearray(), 0
    while pos + 27 <= len(data):
        assert data[pos:pos + 4] == b'OggS'
        nseg = data[pos + 26]
        segs = data[pos + 27:pos + 27 + nseg]
        p = pos + 27 + nseg
        for s in segs:
            cur += data[p:p + s]
            p += s
            if s < 255:
                packets.append(bytes(cur))
                cur = bytearray()
        pos = p
    return packets


def steam_voice_packet(steamid, frames):
    """frames: list of (seq, opus_bytes) або None для маркера кінця фрази (-1)."""
    body = bytearray()
    for f in frames:
        if f is None:
            body += struct.pack('<h', -1)
        else:
            seq, data = f
            body += struct.pack('<hH', len(data), seq) + data
    pkt = bytearray(struct.pack('<Q', steamid))
    pkt += bytes([11]) + struct.pack('<H', 24000)
    pkt += bytes([6]) + struct.pack('<H', len(body)) + body
    pkt += struct.pack('<I', zlib.crc32(bytes(pkt)) & 0xFFFFFFFF)
    return bytes(pkt)


def main():
    out_path, truth_path = sys.argv[1], sys.argv[2]
    pe_bits = 24
    compress = '--compress-userinfo' in sys.argv
    if '--pe-bits' in sys.argv:
        pe_bits = int(sys.argv[sys.argv.index('--pe-bits') + 1])
    rnd = random.Random(1234)

    STEAM_BASE = 76561197960265728
    players = [
        # slot, name, userid, account(Z*2+Y)
        (0, 'Recorder Юзер', 2, 2 * 1111 + 1),
        (1, 'Friend', 3, 2 * 2222 + 0),
        (2, 'Bot01', 4, 0),
    ]
    late_player = (5, 'LateJoiner', 9, 2 * 3333 + 1)

    def guid(account):
        return f'STEAM_0:{account & 1}:{account >> 1}'

    # Мовлення: (slot, account, freq, start_sec, dur_sec)
    speech = [
        (0, players[0][3], 440.0, 2.0, 1.5),     # власний голос (voice_loopback)
        (1, players[1][3], 660.0, 4.0, 2.0),
        (0, players[0][3], 440.0, 7.0, 1.0),
        (1, players[1][3], 660.0, 7.5, 1.2),     # перекриття з попереднім
        (5, late_player[3], 880.0, 10.0, 1.0),   # гравець, що зайшов пізніше
    ]
    total_seconds = 12.5
    total_ticks = int(total_seconds / TICK_INTERVAL)

    # Розклад голосових пакетів: тік -> список (slot, steam_voice_bytes)
    voice_by_tick = {}
    truth_speech = []
    seq_state = {}
    for slot, account, freq, start, dur in speech:
        frames = encode_opus_frames(freq, dur)
        steamid = STEAM_BASE + account
        seq = seq_state.get(slot, 0)
        groups = {}
        last_tick = 0
        for k, fr in enumerate(frames):
            capture_end = start + (k + 1) * 0.020
            # мережевий джитер (UDP-канал Source зберігає порядок пакетів)
            tick = max(last_tick, math.ceil(capture_end / TICK_INTERVAL) + rnd.randint(0, 2))
            last_tick = tick
            groups.setdefault(tick, []).append((seq, fr))
            seq += 1
        ticks = sorted(groups)
        # маркер кінця фрази у останньому пакеті
        groups[ticks[-1]].append(None)
        seq_state[slot] = 0  # після маркера декодер скидається, послідовність починається знову
        for t in ticks:
            voice_by_tick.setdefault(t, []).append((slot, steam_voice_packet(steamid, groups[t])))
        truth_speech.append({'slot': slot, 'steamid64': steamid, 'freq': freq, 'start': start, 'duration': dur,
                             'frames': len(frames)})

    demo = bytearray()

    def cmd_header(cmd, tick):
        demo.extend(struct.pack('<Bi', cmd, tick))

    def packet(cmd, tick, payload):
        cmd_header(cmd, tick)
        demo.extend(b'\x00' * 76)                 # democmdinfo
        demo.extend(struct.pack('<ii', tick, tick))
        demo.extend(struct.pack('<i', len(payload)))
        demo.extend(payload)

    # ---- signon ----------------------------------------------------------------
    w = BitWriter()
    msg_type(w, 3); w.long(0); w.word(0); w.word(0)                        # net_Tick
    msg_type(w, 7); w.string('Welcome to the test server\n')              # svc_Print
    msg_type(w, 8)                                                        # svc_ServerInfo
    w.word(24); w.long(1); w.bit(0); w.bit(1); w.long(0x12345678); w.word(300)
    w.raw(bytes(range(16))); w.byte(0); w.byte(32); w.float(TICK_INTERVAL); w.byte(ord('w'))
    w.string('garrysmod'); w.string('gm_construct'); w.string('painted'); w.string('Test Server [UA]')
    w.string('http://example.com/loading'); w.string('sandbox')
    if GMOD2026:
        w.word(0xFFFF)
    msg_type(w, 10); w.word(300); w.bit(1)                                # svc_ClassInfo
    msg_type(w, 5); w.byte(2); w.string('sv_cheats'); w.string('0'); w.string('sv_allowcslua'); w.string('0')
    # таблиця, яку ми не аналізуємо (для перевірки пропуску)
    other = BitWriter()
    for i in range(10):
        other.bit(1); other.bit(1); other.bit(0); other.string(f'models/test{i}.mdl'); other.bit(0)
    msg_type(w, 12); w.string('modelprecache'); table_max_entries(w, 4096); w.bits(10, q_log2(4096) + 1)
    w.varint32(other.num_bits()); w.bit(0); w.bit(0)
    data = other.getvalue()
    w.random_bits(0, rnd)
    tmp = BitWriter(); tmp.raw(data)
    # копіюємо рівно other.num_bits() біт
    for i in range(other.num_bits()):
        w.bit((data[i >> 3] >> (i & 7)) & 1)
    # userinfo
    max_entries = 256
    ui_entries = []
    for slot, name, uid, account in players:
        ui_entries.append((slot, str(uid), player_info(name, uid, 'BOT' if name.startswith('Bot') else guid(account),
                                                       account, fake=name.startswith('Bot'))))
    uw = userinfo_table_data(ui_entries, max_entries)
    msg_type(w, 12); w.string('userinfo'); table_max_entries(w, max_entries); w.bits(len(ui_entries), q_log2(max_entries) + 1)
    if compress:
        raw_bytes = uw.getvalue()
        comp = lzss_literal_only(raw_bytes)
        blk = BitWriter(); blk.long(len(raw_bytes)); blk.long(len(comp)); blk.raw(comp)
        w.varint32(blk.num_bits()); w.bit(0); w.bit(1)
        bb = blk.getvalue()
        for i in range(blk.num_bits()):
            w.bit((bb[i >> 3] >> (i & 7)) & 1)
    else:
        w.varint32(uw.num_bits()); w.bit(0); w.bit(0)
        ub = uw.getvalue()
        for i in range(uw.num_bits()):
            w.bit((ub[i >> 3] >> (i & 7)) & 1)
    game_event_list(w)                                                    # svc_GameEventList
    msg_type(w, 14); w.string('steam'); w.byte(5)                        # svc_VoiceInit
    msg_type(w, 18); w.bits(1, EDICT_BITS)                                # svc_SetView
    msg_type(w, 6); w.byte(6); w.long(1)                                   # net_SignonState
    packet(1, 0, w.getvalue())

    # dem_datatables (випадкові дані)
    cmd_header(6, 0)
    dt = bytes(rnd.getrandbits(8) for _ in range(5000))
    demo.extend(struct.pack('<i', len(dt)) + dt)
    cmd_header(3, 0)  # synctick

    # ---- ігрові пакети -------------------------------------------------------
    for tick in range(1, total_ticks + 1):
        w = BitWriter()
        msg_type(w, 3); w.long(tick); w.word(100); w.word(5)
        # svc_PacketEntities
        n = rnd.randint(50, 3000)
        msg_type(w, 26); w.bits(4096, EDICT_BITS); w.bit(1); w.long(tick - 1); w.bits(0, 1)
        w.bits(rnd.randint(0, 100), EDICT_BITS); w.bits(n, pe_bits); w.bit(0); w.random_bits(n, rnd)
        if tick % 3 == 0:
            n = rnd.randint(8, 400)
            msg_type(w, 33); w.bits(n + 24, 20); w.byte(0); w.word(rnd.randint(0, 500)); w.random_bits(n, rnd)
        if tick % 5 == 0:
            n = rnd.randint(8, 1000)
            msg_type(w, 27); w.byte(rnd.randint(1, 5)); w.varint32(n); w.random_bits(n, rnd)
        if tick % 7 == 0:
            n = rnd.randint(8, 800)
            msg_type(w, 23); w.byte(rnd.randint(6, 40)); w.bits(n, 11); w.random_bits(n, rnd)
        if tick % 11 == 0:
            n = rnd.randint(8, 600)
            msg_type(w, 25); w.bits(n + 9, 11); w.bits(500, 9); w.random_bits(n, rnd)   # невідома подія
        if tick % 13 == 0:
            n = rnd.randint(8, 500)
            msg_type(w, 17); w.bit(0); w.byte(2); w.bits(n, 16); w.random_bits(n, rnd)
        if tick % 17 == 0:
            n = rnd.randint(8, 200)
            msg_type(w, 17); w.bit(1); w.bits(n, 8); w.random_bits(n, rnd)
        if tick % 97 == 0:
            msg_type(w, 21)   # svc_BSPDecal
            w.bit(1); w.bit(1); w.bit(0)
            w.bit(1); w.bit(1); w.bit(0); w.bits(1234, 14); w.bits(7, 5)
            w.bit(1); w.bit(0); w.bit(1); w.bits(99, 14)
            w.bits(12, 9); w.bit(1); w.bits(0, EDICT_BITS); w.bits(1, 12); w.bit(0)
        if tick % 101 == 0:
            msg_type(w, 19); w.bit(0); w.word(1); w.word(2); w.word(3)
            msg_type(w, 20); w.word(1); w.word(2); w.word(3)
            msg_type(w, 28); w.bits(77, 14)
            msg_type(w, 24); w.bits(5, EDICT_BITS); w.bits(3, 9); w.bits(40, 11); w.random_bits(40, rnd)
            msg_type(w, 31); w.long(42); w.string('cl_interp')
            msg_type(w, 4); w.string('echo hi')
            msg_type(w, 11); w.bit(0)
        if tick == 330:
            # гравець заходить на сервер: svc_UpdateStringTable для userinfo (таблиця №1)
            slot, name, uid, account = late_player
            upd = userinfo_table_data([(slot, str(uid), player_info(name, uid, guid(account), account))], max_entries)
            msg_type(w, 13); w.bits(1, 5); w.bit(0); w.bits(upd.num_bits(), 20)
            ub = upd.getvalue()
            for i in range(upd.num_bits()):
                w.bit((ub[i >> 3] >> (i & 7)) & 1)
        # Чат і події
        if tick == 100:
            user_message(w, 3, say_text(2, 'Привіт усім'))
            game_event(w, 'player_say', {'userid': 3, 'text': 'Привіт усім', 'teamonly': 0})
        if tick == 200:
            user_message(w, 3, say_text(1, 'gg 100%'))
        if tick == 330:
            game_event(w, 'player_connect_client', {'name': late_player[1], 'index': late_player[0],
                                                    'userid': late_player[2], 'networkid': guid(late_player[3]), 'bot': 0})
            game_event(w, 'player_spawn', {'userid': late_player[2]})
        if tick == 400:
            user_message(w, 4, text_msg(3, 'Server restart in 5 minutes\n'))
            user_message(w, 4, text_msg(4, 'Noclip Speed: 600 ups'))    # у центр екрана, не в чат
        if tick == 600:
            game_event(w, 'player_disconnect', {'userid': 3, 'reason': 'Disconnect by user.', 'name': 'Friend',
                                                'networkid': guid(players[1][3]), 'bot': 0})
        if tick == 650:
            user_message(w, 3, say_text(late_player[0] + 1, 'bye'))
        for slot, vp in voice_by_tick.get(tick, []):
            msg_type(w, 15); w.byte(slot); w.byte(0); w.word(len(vp) * 8); w.raw(vp)
        packet(2, tick, w.getvalue())
        if tick % 50 == 0:
            cmd_header(4, tick)
            s = b'say hello\x00'
            demo.extend(struct.pack('<i', len(s)) + s)
    cmd_header(7, total_ticks)

    header = bytearray(1072)
    header[0:8] = b'GMODEMO\x00' if MODERN else b'HL2DEMO\x00'
    struct.pack_into('<ii', header, 8, 3, 24)

    def put_str(off, s):
        b = s.encode('utf-8')[:259]
        header[off:off + len(b)] = b
    put_str(16, 'Test Server [UA]')
    put_str(276, 'Recorder Юзер')
    put_str(536, 'gm_construct')
    put_str(796, 'garrysmod')
    struct.pack_into('<fiii', header, 1056, total_ticks * TICK_INTERVAL, total_ticks, total_ticks, 0)
    open(out_path, 'wb').write(bytes(header) + bytes(demo))
    truth = {'tick_interval': TICK_INTERVAL, 'total_ticks': total_ticks, 'speech': truth_speech,
             'players': [{'slot': p[0], 'name': p[1], 'steamid64': STEAM_BASE + p[3]} for p in players + [late_player]],
             'pe_bits': pe_bits,
             'chat': [{'tick': 100, 'who': 'Friend', 'text': 'Привіт усім'}, {'tick': 200, 'who': 'Recorder Юзер', 'text': 'gg 100%'},
                      {'tick': 650, 'who': 'LateJoiner', 'text': 'bye'}]}
    json.dump(truth, open(truth_path, 'w'), ensure_ascii=False, indent=1)
    print(f'OK: {out_path} ({len(header) + len(demo)} байт, {total_ticks} тіків)')


if __name__ == '__main__':
    main()
