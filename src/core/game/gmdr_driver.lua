-- ============================================================================
--  GMod Demo Render — драйвер рендеру (стан меню GMod). Версія 1.6
--
--  Встановлюється програмою GMod Demo Render у garrysmod/lua/menu/.
--  Скрипт НІЧОГО не робить, якщо немає файлу завдання data/gmdr/job.txt
--  (його створює програма безпосередньо перед запуском гри).
--
--  Черга рендерів: якщо в завданні wait_next, після запису гра не закривається,
--  а чекає наступного job.txt — так кілька фрагментів ідуть без перезапуску гри.
--
--  Видалити: кнопка "Видалити драйвер" у програмі або перевірка цілісності
--  файлів гри в Steam.
--
--  Написано на звичайному Lua 5.1 (без GLua-скорочень), щоб його можна було
--  перевірити будь-яким інтерпретатором Lua.
-- ============================================================================
local JOB_FILE = "gmdr/job.txt"

-- Прочитати і забрати файл завдання; nil — немає або застаріле
local function ReadJob()
	if not file.Exists( JOB_FILE, "DATA" ) then return nil end
	local raw = file.Read( JOB_FILE, "DATA" ) or ""
	file.Delete( JOB_FILE )
	local j = util.JSONToTable( raw )
	if type( j ) ~= "table" or not j.id then return nil end
	if j.created and ( os.time() - tonumber( j.created ) ) > 900 then
		MsgN( "[GMDR] Завдання застаріло, пропускаю" )
		return nil
	end
	return j
end

local job = ReadJob()
if not job then return end

local STATUS_FILE = "gmdr/status_" .. job.id .. ".txt"
local CANCEL_FILE = "gmdr/cancel_" .. job.id .. ".txt"
local MARKS_FILE = "gmdr/marks_" .. job.id .. ".txt"
-- Режим перегляду: демо грає в реальному часі з потрібного місця, без запису;
-- клавіші F9/F11/F6 позначають початок і кінець фрагмента та позначки для програми.
local watch = job.mode == "watch"
local lastJobPoll = 0
local state, stateTime = "menu", SysTime()
local startTick, lastTick, frames = -1, -1, 0
local lastWrite, message = 0, ""
local hideTries = 0
local seekDone = false
local lastSeenTick, tickMoves = -1, 0

local function Status( force )
	if not force and SysTime() - lastWrite < 0.2 then return end
	lastWrite = SysTime()
	local t = {
		id = job.id, state = state, message = message,
		tick = engine.GetDemoPlaybackTick(), total = engine.GetDemoPlaybackTotalTicks(),
		start_tick = startTick, last_tick = lastTick, frames = frames,
		playing = engine.IsPlayingDemo(), time = SysTime()
	}
	file.Write( STATUS_FILE, util.TableToJSON( t ) )
end

local function SetState( s, msg )
	state = s
	stateTime = SysTime()
	message = msg or ""
	MsgN( "[GMDR] стан: ", s, " ", message )
	Status( true )
end

-- Чи виконався конфіг завдання (+exec у командному рядку гри)?
-- Ознака: host_framerate уже має потрібне значення.
local cfgLoaded = nil
local function CheckCfg()
	if cfgLoaded == nil then
		cfgLoaded = math.abs( GetConVarNumber( "host_framerate" ) - ( tonumber( job.host_framerate ) or -1 ) ) < 0.0001
		MsgN( "[GMDR] конфіг завдання ", cfgLoaded and "завантажено" or "не знайдено - використовую прямі команди" )
	end
	return cfgLoaded
end

-- Виконати команду: спершу аліас з конфігу завдання (gmdr_*), інакше напряму,
-- інакше через GameUI ("engine <команда>").
local function Cmd( alias, ... )
	if alias and CheckCfg() and pcall( RunConsoleCommand, alias ) then return true end
	local args = { ... }
	if #args > 0 then
		if pcall( RunConsoleCommand, unpack( args ) ) then return true end
		if pcall( RunGameUICommand, "engine " .. table.concat( args, " " ) ) then return true end
	end
	MsgN( "[GMDR] Не вдалося виконати команду: ", alias or "", " ", table.concat( args, " " ) )
	return false
end

local function ApplyRate()
	local want = tonumber( job.host_framerate ) or 60
	if math.abs( GetConVarNumber( "host_framerate" ) - want ) > 0.0001 then
		pcall( RunConsoleCommand, "host_framerate", tostring( want ) )
	end
end

-- Демо справді грає (а не завантажується): IsInGame() або тік, що рухається.
local function DemoVisible()
	if not engine.IsPlayingDemo() then return false end
	if IsInLoading and IsInLoading() then return false end
	local t = engine.GetDemoPlaybackTick()
	if t ~= lastSeenTick then
		if lastSeenTick >= 0 then tickMoves = tickMoves + 1 end
		lastSeenTick = t
	end
	if IsInGame and IsInGame() then return true end
	return tickMoves >= 3
end

-- Без фокуса (гра у фоні) GMod обмежує FPS змінною fps_max_nofocus — рендер ішов би в кілька
-- разів повільніше. Запам'ятовуємо значення гравця, на час рендеру ставимо 0 (= як fps_max)
-- і повертаємо наприкінці. Змінна зберігається в config.cfg, тож повернути її важливо.
local savedNoFocus = nil
local function UnthrottleBackground()
	if savedNoFocus ~= nil or watch then return end
	savedNoFocus = ""
	if not GetConVarString then return end
	local ok, v = pcall( GetConVarString, "fps_max_nofocus" )
	if not ok or type( v ) ~= "string" or v == "" or v == "0" then return end
	savedNoFocus = v
	pcall( RunConsoleCommand, "fps_max_nofocus", "0" )
end
local function RestoreBackground()
	if savedNoFocus and savedNoFocus ~= "" then pcall( RunConsoleCommand, "fps_max_nofocus", savedNoFocus ) end
	savedNoFocus = ""
end

local function Quit()
	if not pcall( RunGameUICommand, "quit" ) then Cmd( "gmdr_quit", "quit" ) end
end

-- Черга: наступне завдання в уже запущеній грі
local function StartJob( nj )
	job = nj
	watch = job.mode == "watch"
	STATUS_FILE = "gmdr/status_" .. job.id .. ".txt"
	CANCEL_FILE = "gmdr/cancel_" .. job.id .. ".txt"
	MARKS_FILE = "gmdr/marks_" .. job.id .. ".txt"
	startTick, lastTick, frames = -1, -1, 0
	message, hideTries, seekDone = "", 0, false
	lastSeenTick, tickMoves = -1, 0
	savedNoFocus = nil
	-- Конфіг нового завдання (налаштування рендеру) виконуємо самі: гру вже запущено. Його аліаси
	-- gmdr_* мають ті самі назви, що й у попереднього, і перевірити, що exec спрацював, не можна —
	-- тож команди йдуть напряму, а не через аліаси.
	local cfg = "gmdr/job_" .. job.id .. ".cfg"
	if not pcall( RunGameUICommand, "engine exec " .. cfg ) then pcall( RunConsoleCommand, "exec", cfg ) end
	cfgLoaded = false
	MsgN( "[GMDR] Наступне завдання черги: ", job.id )
	SetState( "menu" )
end

-- ---- Перегляд: клавіші-позначки і підказка поверх гри ----
-- F10 у GMod відкриває консоль, F8 — "load quick", тож беремо вільні F9, F11 і F6.
local MARK_KEYS = {
	{ key = KEY_F9 or 100, kind = "start", text = "Початок фрагмента" },
	{ key = KEY_F11 or 102, kind = "end", text = "Кінець фрагмента" },
	{ key = KEY_F6 or 97, kind = "mark", text = "Позначка" },
}
local keyWasDown = {}
local hintText, hintUntil = "", 0
local HasFocus = system and system.HasFocus

local function Hint( text, seconds )
	hintText = text
	hintUntil = SysTime() + ( seconds or 2.5 )
end

local function FormatTime( tick )
	local sec = math.floor( tick * ( tonumber( job.tick_interval ) or ( 1 / 66 ) ) )
	return string.format( "%d:%02d", math.floor( sec / 60 ), sec % 60 )
end

local function PollMarkKeys()
	if not input or not input.IsKeyDown then return end
	if gui.IsGameUIVisible() then return end   -- у меню гри клавіші не рахуються
	if HasFocus and not HasFocus() then return end   -- F9 в іншій програмі — не позначка
	for _, m in ipairs( MARK_KEYS ) do
		local ok, down = pcall( input.IsKeyDown, m.key )
		down = ok and down or false
		if down and not keyWasDown[ m.key ] then
			local tick = engine.GetDemoPlaybackTick()
			local line = m.kind .. " " .. tick .. "\n"
			if file.Append then file.Append( MARKS_FILE, line )
			else file.Write( MARKS_FILE, ( file.Read( MARKS_FILE, "DATA" ) or "" ) .. line ) end
			MsgN( "[GMDR] ", m.text, ": тік ", tick )
			Hint( m.text .. ": " .. FormatTime( tick ) .. " (тік " .. tick .. ")" )
		end
		keyWasDown[ m.key ] = down
	end
end

local function DrawHint()
	if SysTime() > hintUntil or not draw or not draw.SimpleTextOutlined then return end
	-- У стані меню TEXT_ALIGN_* не визначені: без запасних чисел текст ліг би від центру вправо
	pcall( draw.SimpleTextOutlined, "GMod Demo Render — " .. hintText, "DermaLarge", ScrW() / 2, 60,
		Color( 255, 255, 255 ), TEXT_ALIGN_CENTER or 1, TEXT_ALIGN_TOP or 3, 2, Color( 0, 0, 0, 200 ) )
end

file.CreateDir( "gmdr" )
SetState( "menu" )

-- Без фокуса (гра у фоні) рушій GMod щокадру знову відкриває своє меню — як після Alt+Tab.
-- Ховати його в DrawOverlay запізно: цей кадр уже намальовано з меню, і воно потрапляло б у
-- відео через кадр. Think спрацьовує раніше, до малювання кадру.
hook.Add( "Think", "GMDR_HideGameUI", function()
	if watch then
		-- Меню, відкрите гравцем (Esc), не чіпаємо. Але без фокуса рушій відкриває його сам, і
		-- після повернення в гру воно закривало б демо, доки гравець не натисне Esc.
		if state == "watching" then
			if HasFocus and not HasFocus() and gui.IsGameUIVisible() then gui.HideGameUI() end
			PollMarkKeys()
		end
		return
	end
	if ( state == "arming" or state == "recording" or ( state == "loading" and DemoVisible() ) ) and gui.IsGameUIVisible() then
		gui.HideGameUI()
	end
end )

hook.Add( "DrawOverlay", "GMDR_Driver", function()
	local now = SysTime()
	UnthrottleBackground()
	if watch then DrawHint() end

	if state == "menu" then
		if now - stateTime < ( tonumber( job.menu_delay ) or 3 ) then return end
		CheckCfg()
		ApplyRate()
		Cmd( "gmdr_play", "playdemo", job.demo )
		SetState( "loading" )
		return
	end

	if state == "loading" then
		Status()
		if now - stateTime > ( tonumber( job.load_timeout ) or 600 ) then
			SetState( "error", "демо не завантажилось вчасно" )
			return
		end
		-- Демо так і не почало грати і нічого не завантажується: файл не знайдено
		-- або демо записане несумісною версією гри.
		if now - stateTime > 45 and not engine.IsPlayingDemo() and not ( IsInLoading and IsInLoading() ) then
			SetState( "error", "демо не запустилося (несумісна версія гри або файл пошкоджено) - дивіться консоль гри" )
			return
		end
		if not DemoVisible() then return end
		if gui.IsGameUIVisible() then
			hideTries = hideTries + 1
			gui.HideGameUI()
			if hideTries < 200 then return end
		end
		ApplyRate()
		local tick = engine.GetDemoPlaybackTick()
		-- Фрагмент далеко від початку: швидко перемотуємо (demo_gototick), а останні
		-- секунди перед фрагментом програємо звичайно, щоб усе встигло з'явитися.
		local seekTick = tonumber( job.seek_tick ) or -1
		if not seekDone and seekTick > 0 and tick < seekTick then
			seekDone = true
			MsgN( "[GMDR] Перемотую демо до тіку ", seekTick )
			Cmd( "gmdr_seek", "demo_gototick", tostring( seekTick ), "0", "0" )
			return
		end
		if watch then
			SetState( "watching" )
			Hint( "F9 — початок фрагмента, F11 — кінець, F6 — позначка", 8 )
			return
		end
		if tick < ( tonumber( job.start_tick ) or 0 ) then return end
		SetState( "arming" )
		return
	end

	if state == "watching" then
		Status()
		if file.Exists( CANCEL_FILE, "DATA" ) then
			SetState( "quit", "перегляд закрито з програми" )
			Quit()
			return
		end
		if not engine.IsPlayingDemo() then
			SetState( "done", "демо закінчилось" )
		end
		return
	end

	if state == "arming" then
		local flags = job.movie_flags or { "raw" }
		Cmd( "gmdr_start", "startmovie", job.movie, unpack( flags ) )
		frames = 0
		SetState( "recording" )
		return
	end

	if state == "recording" then
		frames = frames + 1
		local tick = engine.GetDemoPlaybackTick()
		if startTick < 0 then startTick = tick end
		lastTick = tick
		ApplyRate()
		if gui.IsGameUIVisible() then gui.HideGameUI() end
		local stop = not engine.IsPlayingDemo()
		local endTick = tonumber( job.end_tick ) or -1
		if endTick > 0 and tick >= endTick then stop = true end
		if file.Exists( CANCEL_FILE, "DATA" ) then
			stop = true
			message = "скасовано користувачем"
		end
		if stop then
			Cmd( "gmdr_stop", "endmovie" )
			SetState( "stopping", message )
			return
		end
		Status()
		return
	end

	if state == "stopping" then
		if now - stateTime < 0.5 then return end
		pcall( RunConsoleCommand, "host_framerate", "0" )
		Cmd( "gmdr_restore" )
		RestoreBackground()
		SetState( "done", message )
		return
	end

	if state == "done" then
		if job.quit and now - stateTime > 1.0 then
			SetState( "quit", message )
			Quit()
		elseif job.wait_next and now - stateTime > 0.5 then
			-- Черга: демо, що ще грає, зупиняємо — і чекаємо наступне завдання
			if engine.IsPlayingDemo() then Cmd( nil, "disconnect" ) end
			SetState( "waiting", message )
		end
		return
	end

	if state == "waiting" then
		if file.Exists( CANCEL_FILE, "DATA" ) or now - stateTime > ( tonumber( job.wait_timeout ) or 600 ) then
			SetState( "quit", "черга закінчилась" )
			Quit()
			return
		end
		Status()
		if now - lastJobPoll < 0.25 then return end
		lastJobPoll = now
		local nj = ReadJob()
		if nj then StartJob( nj ) end
		return
	end

	if state == "error" then
		if job.quit and now - stateTime > 2.0 then
			state = "quit"
			pcall( RunConsoleCommand, "host_framerate", "0" )
			Cmd( "gmdr_restore" )
			RestoreBackground()
			Quit()
		end
	end
end )
