-- ============================================================================
--  GMod Demo Render — драйвер рендеру (стан меню GMod). Версія 1.3
--
--  Встановлюється програмою GMod Demo Render у garrysmod/lua/menu/.
--  Скрипт НІЧОГО не робить, якщо немає файлу завдання data/gmdr/job.txt
--  (його створює програма безпосередньо перед запуском гри).
--
--  Видалити: кнопка "Видалити драйвер" у програмі або перевірка цілісності
--  файлів гри в Steam.
--
--  Написано на звичайному Lua 5.1 (без GLua-скорочень), щоб його можна було
--  перевірити будь-яким інтерпретатором Lua.
-- ============================================================================
local JOB_FILE = "gmdr/job.txt"
if not file.Exists( JOB_FILE, "DATA" ) then return end

local raw = file.Read( JOB_FILE, "DATA" ) or ""
file.Delete( JOB_FILE )
local job = util.JSONToTable( raw )
if type( job ) ~= "table" or not job.id then return end
if job.created and ( os.time() - tonumber( job.created ) ) > 900 then
	MsgN( "[GMDR] Завдання застаріло, пропускаю" )
	return
end

local STATUS_FILE = "gmdr/status_" .. job.id .. ".txt"
local CANCEL_FILE = "gmdr/cancel_" .. job.id .. ".txt"
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
	if savedNoFocus ~= nil then return end
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

file.CreateDir( "gmdr" )
SetState( "menu" )

hook.Add( "DrawOverlay", "GMDR_Driver", function()
	local now = SysTime()
	UnthrottleBackground()

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
		if tick < ( tonumber( job.start_tick ) or 0 ) then return end
		SetState( "arming" )
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
		end
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
