-- ============================================================================
--  driver_mock_test.lua — перевірка логіки Lua-драйвера БЕЗ гри.
--  Імітуємо API меню GMod (file, engine, gui, hook, RunConsoleCommand ...)
--  і проганяємо кілька сценаріїв.
--  Запуск: lua5.1 driver_mock_test.lua ../../src/core/game/gmdr_driver.lua
-- ============================================================================
local DRIVER = arg[1] or "../../src/core/game/gmdr_driver.lua"

-- ---------- мінімальний JSON ----------
local function json_encode(v)
	local t = type(v)
	if t == "table" then
		if #v > 0 then
			local parts = {}
			for _, x in ipairs(v) do parts[#parts + 1] = json_encode(x) end
			return "[" .. table.concat(parts, ",") .. "]"
		end
		local parts = {}
		for k, x in pairs(v) do parts[#parts + 1] = string.format("%q", tostring(k)) .. ":" .. json_encode(x) end
		return "{" .. table.concat(parts, ",") .. "}"
	elseif t == "string" then return string.format("%q", v)
	elseif t == "boolean" then return v and "true" or "false"
	elseif t == "nil" then return "null"
	else return tostring(v) end
end

local function json_decode(s)
	local pos = 1
	local function ws() pos = s:find("[^%s]", pos) or #s + 1 end
	local value
	local function str()
		local out = {}
		pos = pos + 1
		while true do
			local c = s:sub(pos, pos)
			if c == '"' then pos = pos + 1 break end
			if c == "\\" then pos = pos + 1; c = s:sub(pos, pos) end
			out[#out + 1] = c
			pos = pos + 1
		end
		return table.concat(out)
	end
	function value()
		ws()
		local c = s:sub(pos, pos)
		if c == "{" then
			local t = {}
			pos = pos + 1
			ws()
			if s:sub(pos, pos) == "}" then pos = pos + 1 return t end
			while true do
				ws()
				local k = str()
				ws(); pos = pos + 1 -- :
				t[k] = value()
				ws()
				local d = s:sub(pos, pos); pos = pos + 1
				if d == "}" then return t end
			end
		elseif c == "[" then
			local t = {}
			pos = pos + 1
			ws()
			if s:sub(pos, pos) == "]" then pos = pos + 1 return t end
			while true do
				t[#t + 1] = value()
				ws()
				local d = s:sub(pos, pos); pos = pos + 1
				if d == "]" then return t end
			end
		elseif c == '"' then return str()
		elseif s:sub(pos, pos + 3) == "true" then pos = pos + 4 return true
		elseif s:sub(pos, pos + 4) == "false" then pos = pos + 5 return false
		elseif s:sub(pos, pos + 3) == "null" then pos = pos + 4 return nil
		else
			local num = s:match("^-?[%d%.eE+-]+", pos)
			pos = pos + #num
			return tonumber(num)
		end
	end
	return value()
end

-- ---------- сценарій ----------
local failures = 0
local function check(cond, what)
	if cond then print("  OK   " .. what) else print("  ПРОВАЛ " .. what); failures = failures + 1 end
end

local function run_scenario(name, opts)
	print("[" .. name .. "]")
	local TI = 1 / 66
	local TOTAL = 825
	local files = {}
	local now = 100.0
	local cvars = { host_framerate = opts.cfg_loaded and opts.rate or 0, fps_max_nofocus = 20 }
	local log = {}
	local S = { loading_until = nil, playing = false, tick = 0, ui_visible = 0, recording = false, quit = false,
	            starts = 0, stops = 0, start_at = nil, stop_at = nil }
	local hooks = {}

	local job = { id = "abc", created = os.time() - (opts.stale and 5000 or 1), demo = "gmdr_tmp/abc/demo",
	              movie = "gmdr_tmp/abc/f", movie_flags = { "raw" }, host_framerate = opts.rate,
	              start_tick = opts.start_tick or 0, end_tick = opts.end_tick or -1, quit = true, menu_delay = 3,
	              load_timeout = 600, seek_tick = opts.seek_tick or -1, mode = opts.watch and "watch" or nil,
	              tick_interval = TI }
	if opts.watch then job.host_framerate = 0; job.quit = false end
	files["gmdr/job.txt"] = json_encode(job)

	local env = {}
	setmetatable(env, { __index = _G })
	env.file = {
		Exists = function(p) return files[p] ~= nil end,
		Read = function(p) return files[p] end,
		Write = function(p, c) files[p] = c end,
		Delete = function(p) files[p] = nil end,
		Append = function(p, c) files[p] = (files[p] or "") .. c end,
		CreateDir = function() end,
	}
	env.util = { JSONToTable = json_decode, TableToJSON = json_encode }
	env.SysTime = function() return now end
	env.MsgN = function(...) log[#log + 1] = table.concat({ ... }) end
	env.GetConVarNumber = function(n) return cvars[n] or 0 end
	env.GetConVarString = function(n) return cvars[n] ~= nil and tostring(cvars[n]) or "" end
	local function exec(cmd, args)
		log[#log + 1] = "CMD " .. cmd .. " " .. table.concat(args, " ")
		if opts.blocked and opts.blocked[cmd] then error("RunConsoleCommand: Command is blocked! (" .. cmd .. ")") end
		if cmd == "host_framerate" then cvars.host_framerate = tonumber(args[1]) end
		if cmd == "fps_max_nofocus" then
			cvars.fps_max_nofocus = tonumber(args[1])
			if S.recording and cvars.fps_max_nofocus ~= 0 then S.nofocus_during_recording = true end
		end
		if cmd == "gmdr_play" or cmd == "playdemo" then
			if cmd == "gmdr_play" and not opts.cfg_loaded then return end -- аліасу немає
			S.loading_until = now + 2
		elseif cmd == "gmdr_start" or cmd == "startmovie" then
			if cmd == "gmdr_start" and not opts.cfg_loaded then return end
			S.recording = true; S.starts = S.starts + 1; S.start_at = S.tick
			S.start_args = args
		elseif cmd == "gmdr_stop" or cmd == "endmovie" then
			if cmd == "gmdr_stop" and not opts.cfg_loaded then return end
			S.recording = false; S.stops = S.stops + 1; S.stop_at = S.tick
		elseif cmd == "gmdr_seek" or cmd == "demo_gototick" then
			if cmd == "gmdr_seek" and not opts.cfg_loaded then return end
			S.seeks = (S.seeks or 0) + 1
			S.tick = cmd == "gmdr_seek" and opts.seek_tick or tonumber(args[1])
			S.seek_before_start = not S.recording
		elseif cmd == "gmdr_restore" then S.restored = true
		elseif cmd == "quit" or cmd == "gmdr_quit" then S.quit = true end
	end
	env.RunConsoleCommand = function(cmd, ...) exec(cmd, { ... }) end
	env.RunGameUICommand = function(c)
		log[#log + 1] = "GAMEUI " .. c
		if c == "quit" then S.quit = true return end
		local cmd, rest = c:match("^engine (%S+)%s*(.*)$")
		if cmd then
			local args = {}
			for a in rest:gmatch("%S+") do args[#args + 1] = a end
			local saved = opts.blocked
			opts.blocked = nil -- через GameUI блокування немає
			exec(cmd, args)
			opts.blocked = saved
		end
	end
	env.engine = {
		IsPlayingDemo = function() return S.playing end,
		GetDemoPlaybackTick = function() return math.floor(S.tick) end,
		GetDemoPlaybackTotalTicks = function() return TOTAL end,
	}
	env.gui = {
		IsGameUIVisible = function() return S.ui_visible > 0 end,
		HideGameUI = function() S.ui_visible = S.ui_visible - 1 end,
	}
	env.IsInGame = function() return (not opts.no_ingame) and S.playing end
	S.keys = {}
	env.input = { IsKeyDown = function(k) return S.keys[k] == true end }
	env.system = { HasFocus = function() return not S.unfocused end }
	-- У стані меню TEXT_ALIGN_* немає (у _G тестового Lua їх теж немає)
	env.ScrW = function() return 1280 end
	env.Color = function(r, g, b, a) return { r, g, b, a } end
	env.draw = { SimpleTextOutlined = function(text, font, x, y, col, xa, ya) S.hint_xalign = xa end }
	env.IsInLoading = function() return S.loading_until ~= nil and now < S.loading_until end
	env.hook = { Add = function(ev, id, fn) hooks[ev] = fn end }

	local chunk = assert(loadfile(DRIVER))
	setfenv(chunk, env)
	chunk()

	if not hooks.DrawOverlay then
		check(opts.expect_inert, "драйвер неактивний без свіжого завдання")
		return
	end
	check(not opts.expect_inert, "драйвер активувався")
	check(files["gmdr/job.txt"] == nil, "файл завдання видалено")

	for frame = 1, 200000 do
		-- симуляція гри
		if S.loading_until and now >= S.loading_until and not S.playing and S.tick == 0 then
			S.playing = true
			S.ui_visible = 3 -- меню ще показане кілька кадрів
		end
		if S.playing and not (S.loading_until and now < S.loading_until) then
			local rate = cvars.host_framerate > 0 and cvars.host_framerate or 60
			S.tick = S.tick + (1 / rate) / TI
			if S.tick >= TOTAL then S.playing = false end
		end
		if opts.cancel_at and S.tick >= opts.cancel_at then files["gmdr/cancel_abc.txt"] = "1" end
		-- натискання клавіш (тримаються кілька кадрів, як справжні)
		S.keys = {}
		for _, p in ipairs(opts.press or {}) do
			if S.tick >= p.tick and S.tick < p.tick + 5 then S.keys[p.key] = true end
		end
		if S.game_ui_at and S.tick >= S.game_ui_at then S.ui_visible = 1 end
		-- Гра у фоні (Alt+Tab): рушій щокадру знову відкриває своє меню; F9 в іншій програмі
		S.unfocused = opts.unfocus and S.tick >= opts.unfocus[1] and S.tick < opts.unfocus[2] or false
		if S.unfocused then
			S.ui_visible = 1
			if opts.unfocus[3] then S.keys[opts.unfocus[3]] = true end
		end
		if hooks.Think then hooks.Think() end
		hooks.DrawOverlay()
		now = now + 0.01
		if S.quit then break end
	end
	local st = json_decode(files["gmdr/status_abc.txt"] or "{}")
	if opts.watch then
		local marks = files["gmdr/marks_abc.txt"] or ""
		local kinds = {}
		for kind, tick in marks:gmatch("(%a+) (%d+)") do kinds[#kinds + 1] = { kind = kind, tick = tonumber(tick) } end
		check(S.starts == 0, "у режимі перегляду нічого не записується")
		check(#kinds == #opts.press, "кожне натискання — рівно одна позначка (" .. #kinds .. ")")
		for i, p in ipairs(opts.press) do
			local k = kinds[i] or {}
			check(k.kind == p.kind and k.tick and k.tick >= p.tick and k.tick < p.tick + 3,
				"позначка " .. p.kind .. " на тіку " .. tostring(k.tick))
		end
		check(cvars.host_framerate == 0, "перегляд у реальному часі (host_framerate 0)")
		check(cvars.fps_max_nofocus == 20, "fps_max_nofocus не змінено")
		check(S.seeks == 1, "перемотано до місця перегляду")
		check(not S.quit or opts.cancel_at, "гра не закривається сама після перегляду")
		check(st.state == (opts.cancel_at and "quit" or "done"), "підсумковий статус (" .. tostring(st.state) .. ")")
		check(S.hint_xalign == 1, "підказка по центру екрана (" .. tostring(S.hint_xalign) .. ")")
		if opts.verbose then for _, l in ipairs(log) do print("     " .. l) end end
		return
	end
	check(S.quit, "гра закрита після завершення")
	check(S.starts == 1, "startmovie виконано рівно один раз")
	check(S.stops == 1, "endmovie виконано рівно один раз")
	check(S.start_at and S.start_at >= (opts.start_tick or 0), "запис почався не раніше start_tick (" .. tostring(S.start_at) .. ")")
	check(S.restored or not opts.cfg_loaded, "налаштування відновлено (gmdr_restore)")
	check(not S.nofocus_during_recording, "під час запису fps_max_nofocus = 0 (гра у фоні не гальмує)")
	check(cvars.fps_max_nofocus == 20, "fps_max_nofocus повернуто гравцю (" .. tostring(cvars.fps_max_nofocus) .. ")")
	check(st.state == "quit", "підсумковий статус 'quit' (" .. tostring(st.state) .. ")")
	check(st.start_tick and st.start_tick >= (opts.start_tick or 0), "у статусі є тік першого кадру (" .. tostring(st.start_tick) .. ")")
	if opts.cfg_loaded then
		local direct = false
		for _, l in ipairs(log) do if l:find("^CMD startmovie") then direct = true end end
		check(not direct, "використано аліаси, а не прямі команди")
	else
		check(S.start_args and S.start_args[1] == "gmdr_tmp/abc/f" and S.start_args[2] == "raw", "прямий startmovie з правильними аргументами")
	end
	if opts.end_tick then check(S.stop_at >= opts.end_tick and S.stop_at < opts.end_tick + 2, "зупинка на end_tick (" .. S.stop_at .. ")") end
	if opts.cancel_at then check(S.stop_at < TOTAL - 10 and st.message ~= "", "скасування зупинило запис (" .. S.stop_at .. ")") end
	if opts.seek_tick then
		check(S.seeks == 1 and S.seek_before_start, "перемотування виконано один раз, до початку запису")
		check(S.start_at >= opts.start_tick and S.start_at < opts.start_tick + 2, "запис почався рівно на start_tick після перемотування (" .. tostring(S.start_at) .. ")")
	else
		check(not S.seeks, "без перемотування, коли воно не потрібне")
	end
	if opts.verbose then for _, l in ipairs(log) do print("     " .. l) end end
end

run_scenario("звичайний запуск (конфіг завантажено)", { cfg_loaded = true, rate = 240, start_tick = 100 })
run_scenario("конфіг не виконався — прямі команди", { cfg_loaded = false, rate = 120 })
run_scenario("діапазон тіків", { cfg_loaded = true, rate = 60, start_tick = 200, end_tick = 500 })
run_scenario("скасування", { cfg_loaded = true, rate = 60, cancel_at = 300 })
run_scenario("startmovie заблоковано для Lua — запасний шлях через GameUI",
	{ cfg_loaded = false, rate = 60, blocked = { startmovie = true, playdemo = true, endmovie = true, quit = true } })
run_scenario("застаріле завдання", { cfg_loaded = true, rate = 60, stale = true, expect_inert = true })
run_scenario("перемотування до далекого фрагмента", { cfg_loaded = true, rate = 60, start_tick = 600, end_tick = 700, seek_tick = 400 })
run_scenario("перемотування без конфігу (прямий demo_gototick)", { cfg_loaded = false, rate = 60, start_tick = 600, end_tick = 700, seek_tick = 400 })
run_scenario("IsInGame() не працює під час демо", { cfg_loaded = true, rate = 60, start_tick = 50, no_ingame = true })

run_scenario("перегляд у грі з позначками F9/F11/F6", { cfg_loaded = true, watch = true, start_tick = 300, seek_tick = 250,
	press = { { tick = 320, key = 100, kind = "start" }, { tick = 400, key = 97, kind = "mark" }, { tick = 500, key = 102, kind = "end" } } })
run_scenario("перегляд закрито з програми", { cfg_loaded = true, watch = true, start_tick = 300, seek_tick = 250,
	press = { { tick = 350, key = 100, kind = "start" } }, cancel_at = 600 })
run_scenario("перегляд: гра у фоні, меню гри не закриває демо після повернення", { cfg_loaded = true, watch = true,
	start_tick = 300, seek_tick = 250, unfocus = { 330, 420, 100 },
	press = { { tick = 320, key = 97, kind = "mark" }, { tick = 450, key = 102, kind = "end" } } })

print(failures == 0 and "\nУСІ СЦЕНАРІЇ ПРОЙДЕНО" or ("\nПРОВАЛІВ: " .. failures))
os.exit(failures == 0 and 0 or 1)
