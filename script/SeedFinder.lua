-- ============================================================================
-- SeedFinder — Minecraft Bedrock Structure Finder
-- Uses network.getAsync() to call the SeedFinder API. Works with either the
-- local Flask server (127.0.0.1:7890) or the hosted API at
-- mineseedfinder.vercel.app. Leave the Server URL field empty to use the
-- hosted server.
-- ============================================================================

name = "SeedFinder"
description = "See structure of the map with the seed, fully integrated"
author = "zebedelu"
version = "1.4.1"

-- ============================================================================
-- Section 1: HTTP Bridge Integration
-- ============================================================================

local HOSTED_URL = "https://mineseedfinder.vercel.app"
local SERVER_URL = HOSTED_URL
local serverOnline = false
local serverWarned = false
local rateLimited = false
local rateLimitedWarned = false
local lastServerCheck = 0

-- In-flight guards: network.getAsync is non-blocking, but we still must not
-- fire a second request while one is already resolving.
local statusRequestInFlight = false
local scanRequestInFlight = false

-- Detect HTTP 429 (Vercel firewall rate limit). network.get only returns the
-- body, so we look for the markers Vercel/CF put in rate-limit responses.
local function isRateLimited(response, statusCode)
	if statusCode == 429 then return true end
	if not response or type(response) ~= "string" then return false end
	local lower = response:lower()
	return lower:find("429") ~= nil
		or lower:find("too many requests") ~= nil
		or lower:find("rate limit") ~= nil
end

local function checkServerAsync()
	local now = os.clock()
	if now - lastServerCheck < 30 then return end
	if statusRequestInFlight then return end
	lastServerCheck = now
	statusRequestInFlight = true

	network.getAsync(SERVER_URL .. "/status", function(response, statusCode, success)
		statusRequestInFlight = false

		if success and response and type(response) == "string" and response ~= "" and response ~= "null" then
			if response:find('"ok"') or response:find('"status"') then
				serverOnline = true
				serverWarned = false
			elseif isRateLimited(response, statusCode) then
				serverOnline = true
				rateLimited = true
				serverWarned = false
			else
				serverOnline = false
			end
		else
			serverOnline = false
			if not serverWarned then
				serverWarned = true
				log("SeedFinder: Failed to reach server at " .. SERVER_URL)
			end
		end
	end)
end

-- Update check for the script itself: compares our `version` field with the
-- one on GitHub (raw file on main). One fetch per session, silent on failure.
local RAW_SCRIPT_URL = "https://raw.githubusercontent.com/zebedelu/SeedFinder/main/script/SeedFinder.lua"
local scriptUpdate = nil

local function versionIsNewer(a, b)
	local pa, pb = {}, {}
	for n in a:gmatch("%d+") do pa[#pa + 1] = tonumber(n) end
	for n in b:gmatch("%d+") do pb[#pb + 1] = tonumber(n) end
	for i = 1, math.max(#pa, #pb) do
		local x, y = pa[i] or 0, pb[i] or 0
		if x > y then return true end
		if x < y then return false end
	end
	return false
end

local function checkScriptUpdateAsync()
	network.getAsync(RAW_SCRIPT_URL, function(response, statusCode, success)
		if not success or type(response) ~= "string" then return end
		local remote = response:match('version%s*=%s*"(%d+%.%d+%.%d+)"')
		if remote and versionIsNewer(remote, version) then
			scriptUpdate = remote
			log("SeedFinder: new script version available: " .. remote)
		end
	end)
end

-- ============================================================================
-- Section 2: Utility Functions
-- ============================================================================

local STRUCTURE_TYPES = {
	Desert_Pyramid = 1,
	Jungle_Temple = 2,
	Swamp_Hut = 3,
	Igloo = 4,
	Village = 5,
	Ocean_Ruin = 6,
	Shipwreck = 7,
	Monument = 8,
	Mansion = 9,
	Outpost = 10,
	Ruined_Portal = 11,
	Ruined_Portal_N = 12,
	Ancient_City = 13,
	Treasure = 14,
	Mineshaft = 15,
	Trail_Ruins = 23,
	Trial_Chambers = 24,
}

local STRUCTURE_ICONS = {
	desert_pyramid = "[D]",
	jungle_pyramid = "[J]",
	swamp_hut = "[S]",
	igloo = "[I]",
	village = "[V]",
	ocean_ruin = "[R]",
	shipwreck = "[W]",
	monument = "[O]",
	mansion = "[M]",
	pillager_outpost = "[P]",
	ruinedportal = "[RP]",
	ruined_portal_nether = "[NRP]",
	ancient_city = "[A]",
	buried_treasure = "[B]",
	mineshaft = "[MS]",
	trail_ruins = "[T]",
	trial_chambers = "[TC]",
	unknown = "[?]",
}

local STRUCTURE_DISPLAY_NAMES = {
	desert_pyramid = "Desert Pyramid",
	jungle_pyramid = "Jungle Temple",
	swamp_hut = "Swamp Hut",
	igloo = "Igloo",
	village = "Village",
	ocean_ruin = "Ocean Ruin",
	shipwreck = "Shipwreck",
	monument = "Ocean Monument",
	mansion = "Woodland Mansion",
	pillager_outpost = "Pillager Outpost",
	ruinedportal = "Ruined Portal",
	ruined_portal_nether = "Ruined Portal (Nether)",
	ancient_city = "Ancient City",
	buried_treasure = "Buried Treasure",
	mineshaft = "Mineshaft",
	trail_ruins = "Trail Ruins",
	trial_chambers = "Trial Chambers",
	unknown = "Unknown Structure",
}

local function getStructureIcon(name)
	return STRUCTURE_ICONS[name] or STRUCTURE_ICONS.unknown
end

local function getStructureDisplayName(name)
	return STRUCTURE_DISPLAY_NAMES[name] or name
end

local function parseSeed(input)
	if not input or input == "" then return nil end
	local seed = tonumber(input)
	if seed then return seed end
	-- Hash string to seed (Java's String.hashCode equivalent)
	local hash = 0
	for i = 1, #input do
		hash = (hash * 31 + string.byte(input, i)) % 2 ^ 32
	end
	if hash >= 2 ^ 31 then hash = hash - 2 ^ 32 end
	return hash
end

-- Safe setting value readers (Flarial may return unexpected types after persistence)
local function getNum(setting, default)
	local v = setting and setting.value
	if type(v) == "number" then return v end
	if type(v) == "string" then return tonumber(v) or default end
	return default
end

local function getStr(setting, default)
	local v = setting and setting.value
	if type(v) == "string" then return v end
	if type(v) == "number" then return tostring(v) end
	return default
end

local function getBool(setting, default)
	local v = setting and setting.value
	if type(v) == "boolean" then return v end
	if type(v) == "number" then return v ~= 0 end
	if type(v) == "string" then return v == "true" end
	return default
end

local function formatDistance(dist)
	if dist < 1 then return "<1" end
	return string.format("%.0f", dist)
end

-- Parse JSON response from the server (no json.decode in sandbox)
-- Flask returns fields in order: distance, name, x, z
-- But we match both orderings to be safe
local function parseScanResponse(jsonStr)
	local results = {}

	if not jsonStr or jsonStr == "" then return results end

	local seen = {}

	-- Pattern A: "distance", "name", "x", "z" (Flask actual order)
	for dist, name, x, z in jsonStr:gmatch(
		'"distance"%s*:%s*([%d%.]+)%s*,%s*"name"%s*:%s*"([^"]+)"%s*,%s*"x"%s*:%s*(-?%d+)%s*,%s*"z"%s*:%s*(-?%d+)'
	) do
		local key = name .. "_" .. x .. "_" .. z
		if not seen[key] then
			seen[key] = true
			table.insert(results, {
				name = name,
				x = tonumber(x),
				z = tonumber(z),
				distance = tonumber(dist),
				displayName = getStructureDisplayName(name),
				icon = getStructureIcon(name),
			})
		end
	end

	-- Pattern B: "name", "x", "z", "distance" (alternative order)
	for name, x, z, dist in jsonStr:gmatch(
		'"name"%s*:%s*"([^"]+)"%s*,%s*"x"%s*:%s*(-?%d+)%s*,%s*"z"%s*:%s*(-?%d+)%s*,%s*"distance"%s*:%s*([%d%.]+)'
	) do
		local key = name .. "_" .. x .. "_" .. z
		if not seen[key] then
			seen[key] = true
			table.insert(results, {
				name = name,
				x = tonumber(x),
				z = tonumber(z),
				distance = tonumber(dist),
				displayName = getStructureDisplayName(name),
				icon = getStructureIcon(name),
			})
		end
	end

	return results
end

-- ============================================================================
-- Section 3: Settings
-- ============================================================================

local seedTextBox = settings.addTextBox("Seed", "Enter your world seed (numbers only)", "1", 30)
local radiusSlider = settings.addSlider("Radius", "How far to search (chunks)", 10, 200, 1)
local maxResultsSlider = settings.addSlider("Max Results", "Maximum structures to display", 15, 50, 1)
local serverUrlTextBox = settings.addTextBox("Server URL", "Leave empty to connect to the hosted server (mineseedfinder.vercel.app)", "", 40)
local rescanKey = settings.addKeybind("Rescan", "Press to clear and rescan structures")
local notifyToggle = settings.addToggle("Scan Notification", "Show a notification when scan completes", true)

local toggleVillage = settings.addToggle("Village", "Search for villages", true)
local toggleDesertPyramid = settings.addToggle("Desert Pyramid", "Search for desert pyramids", true)
local toggleJungleTemple = settings.addToggle("Jungle Temple", "Search for jungle temples", true)
local toggleSwampHut = settings.addToggle("Swamp Hut", "Search for swamp huts", true)
local toggleOutpost = settings.addToggle("Pillager Outpost", "Search for pillager outposts", true)
local toggleIgloo = settings.addToggle("Igloo", "Search for igloos", true)
local toggleOceanMonument = settings.addToggle("Ocean Monument", "Search for ocean monuments", true)
local toggleOceanRuin = settings.addToggle("Ocean Ruin", "Search for ocean ruins", true)
local toggleMansion = settings.addToggle("Woodland Mansion", "Search for woodland mansions", true)
local toggleAncientCity = settings.addToggle("Ancient City", "Search for ancient cities", true)
local toggleTrailRuins = settings.addToggle("Trail Ruins", "Search for trail ruins", true)
local toggleRuinedPortal = settings.addToggle("Ruined Portal", "Search for ruined portals", true)
local toggleBuriedTreasure = settings.addToggle("Buried Treasure", "Search for buried treasure", true)

-- Toggle -> structure type ID mapping
local TOGGLE_TYPE_MAP = {
	{toggle = toggleVillage, id = STRUCTURE_TYPES.Village},
	{toggle = toggleDesertPyramid, id = STRUCTURE_TYPES.Desert_Pyramid},
	{toggle = toggleJungleTemple, id = STRUCTURE_TYPES.Jungle_Temple},
	{toggle = toggleSwampHut, id = STRUCTURE_TYPES.Swamp_Hut},
	{toggle = toggleOutpost, id = STRUCTURE_TYPES.Outpost},
	{toggle = toggleIgloo, id = STRUCTURE_TYPES.Igloo},
	{toggle = toggleOceanMonument, id = STRUCTURE_TYPES.Monument},
	{toggle = toggleOceanRuin, id = STRUCTURE_TYPES.Ocean_Ruin},
	{toggle = toggleMansion, id = STRUCTURE_TYPES.Mansion},
	{toggle = toggleAncientCity, id = STRUCTURE_TYPES.Ancient_City},
	{toggle = toggleTrailRuins, id = STRUCTURE_TYPES.Trail_Ruins},
	{toggle = toggleRuinedPortal, id = STRUCTURE_TYPES.Ruined_Portal},
	{toggle = toggleBuriedTreasure, id = STRUCTURE_TYPES.Treasure},
}

-- ============================================================================
-- Section 4: Module State
-- ============================================================================

local scanResults = {}
local currentSeed = nil
local needsRescan = true
local lastPlayerX = 0
local lastPlayerZ = 0
local lastDimension = ""
local rescanKeyHeld = false

-- ============================================================================
-- Section 5: Async scan request
-- ============================================================================

local function fireScanRequest()
	if scanRequestInFlight then return end
	if not currentSeed then return end

	-- Build types list (use safe reader for toggle persistence)
	local typeIds = {}
	for _, entry in ipairs(TOGGLE_TYPE_MAP) do
		if getBool(entry.toggle, true) then
			table.insert(typeIds, entry.id)
		end
	end

	if #typeIds == 0 then
		scanResults = {}
		needsRescan = false
		return
	end

	-- Build URL (use getNum + math.floor for integer values)
	local typesStr = table.concat(typeIds, ",")
	local seedStr = string.format("%.0f", currentSeed)
	local scanUrl = string.format(
		"%s/scan?seed=%s&x=%.1f&z=%.1f&radius=%d&max=%d&types=%s",
		SERVER_URL,
		seedStr,
		lastPlayerX,
		lastPlayerZ,
		math.floor(getNum(radiusSlider, 10)),
		math.floor(getNum(maxResultsSlider, 15)),
		typesStr
	)

	scanRequestInFlight = true
	needsRescan = false -- claimed by this in-flight request; re-armed on failure below

	network.getAsync(scanUrl, function(response, statusCode, success)
		scanRequestInFlight = false

		if success and response and type(response) == "string" and response ~= "" and response ~= "null" then
			local parsed = parseScanResponse(response)
			if #parsed > 0 or response:find('"results"') then
				scanResults = parsed
				rateLimited = false
				rateLimitedWarned = false
				serverOnline = true
				serverWarned = false
				if getBool(notifyToggle, true) then
					client.notify(string.format("Scan complete! %d structures found", #scanResults))
				end
			elseif isRateLimited(response, statusCode) then
				rateLimited = true
				serverOnline = true
				serverWarned = false
				if not rateLimitedWarned then
					rateLimitedWarned = true
					log("SeedFinder: Rate limited (60 requests/min). Keeping previously scanned structures.")
					client.notify("SeedFinder: Rate limited! Max 60 requests per minute. Keeping previous results.")
				end
			else
				if not serverWarned then
					serverWarned = true
					log("SeedFinder: Failed to reach server at " .. SERVER_URL)
				end
				serverOnline = false
				rateLimited = false
			end
		else
			if not serverWarned then
				serverWarned = true
				log("SeedFinder: Failed to reach server at " .. SERVER_URL)
			end
			serverOnline = false
			rateLimited = false
			-- Request failed outright — re-arm so the next tick tries again
			-- instead of getting stuck forever.
			needsRescan = true
		end
	end)
end

-- ============================================================================
-- Section 6: TickEvent Handler
-- ============================================================================

local function onTick()
	-- Check if player is in a world by verifying position is available
	local px, py, pz = player.position()
	if not px or px == 0.0 and py == 0.0 and pz == 0.0 then return end

	-- Update server URL if changed (use safe reader for persistence).
	-- Empty field = use the hosted server.
	local url = getStr(serverUrlTextBox, "")
	if url and url ~= "" then
		SERVER_URL = url:gsub("/+$", "") -- trim trailing slash
	else
		SERVER_URL = HOSTED_URL
	end

	-- Check dimension change
	local dim = player.dimension()
	if dim ~= lastDimension then
		needsRescan = true
		lastDimension = dim or ""
	end

	-- Check seed change (use safe reader for persistence)
	local seed = parseSeed(getStr(seedTextBox, "1"))
	if seed ~= currentSeed then
		currentSeed = seed
		needsRescan = true
	end

	-- Check if player moved enough to warrant rescan
	if px and pz then
		local dx = px - lastPlayerX
		local dz = pz - lastPlayerZ
		local moveDist = math.sqrt(dx * dx + dz * dz)
		local radius = math.floor(getNum(radiusSlider, 10))
		if moveDist > radius * 8 then
			needsRescan = true
		end
		lastPlayerX = px
		lastPlayerZ = pz
	end

	-- Check rescan keybind (KeybindSetting.value = true when held)
	local rescanKeyDown = getBool(rescanKey, false)
	if rescanKeyDown and not rescanKeyHeld then
		needsRescan = true
	end
	rescanKeyHeld = rescanKeyDown

	-- Keep the status check alive independently (throttled to every 30s inside checkServerAsync)
	checkServerAsync()

	if not needsRescan then return end
	if not currentSeed then return end

	fireScanRequest()
end

-- ============================================================================
-- Section 7: RenderEvent Handler
-- ============================================================================

local function onRender()
	-- Only render if we have a position (player is in a world)
	local px, py, pz = player.position()
	if not px then return end

	ImGui.SetNextWindowSize({350, 360}, 4)
	ImGui.SetNextWindowBgAlpha(0.6)
	ImGui.Begin("SeedFinder")

	if rateLimited then
		ImGui.Text("Rate limited! Max 60 requests/min.")
		ImGui.Text("API: " .. SERVER_URL)
		ImGui.Text("Keeping previously scanned structures.")
	elseif not serverOnline then
		ImGui.Text("Server offline!")
		ImGui.Text("URL: " .. SERVER_URL)
	elseif not currentSeed then
		ImGui.Text("Enter a seed in settings to begin")
	end

	-- Always keep previously scanned structures on screen
	if currentSeed and #scanResults > 0 then
		ImGui.Text(string.format("Nearby Structures (%d)", #scanResults))
		ImGui.Text("--------------------------------")
		for i, result in ipairs(scanResults) do
			local icon = result.icon or "[?]"
			local displayName = result.displayName or result.name
			local distStr = formatDistance(result.distance)

			ImGui.BulletText(string.format(
				"%s %s (X:%.0f Z:%.0f) - %s chunks",
				icon, displayName, result.x, result.z, distStr
			))
		end
	elseif currentSeed and not rateLimited and serverOnline then
		ImGui.Text("No structures found nearby")
		ImGui.Text(string.format("Seed: %.0f | Radius: %d chunks", currentSeed, math.floor(getNum(radiusSlider, 10))))
	end

	ImGui.Text("--------------------------------")
	ImGui.Text("Server: " .. SERVER_URL)
	ImGui.Text("Leave the Server URL field empty to connect to the hosted server!")
	ImGui.Text("Max 60 requests per minute.")
	ImGui.Text("Check mineseedfinder.vercel.app for possible updates! 😊")
	ImGui.Text("If you really like this project, give a star on our GitHub!")
	ImGui.Text("https://github.com/zebedelu/SeedFinder")
	if scriptUpdate then
		ImGui.Text("New script version: " .. scriptUpdate .. " - github.com/zebedelu/SeedFinder")
	end

	ImGui.End()
end

-- ============================================================================
-- Section 8: Module Lifecycle
-- ============================================================================

function onLoad()
	log("SeedFinder loaded")
	log("Server URL field empty = hosted API at " .. HOSTED_URL)
	checkScriptUpdateAsync()
end

function onEnable()
	needsRescan = true
	lastDimension = ""
	scanResults = {}
	statusRequestInFlight = false
	scanRequestInFlight = false
	lastServerCheck = 0 -- force an immediate status check on the next tick

	checkServerAsync()
end

function onDisable()
	-- Save settings immediately when module is disabled
	scanResults = {}
end

-- ============================================================================
-- Section 9: Chat Commands
-- ============================================================================

registerCommand("seedscan", function()
	needsRescan = true
	log("SeedFinder: Rescanning...")
end)

-- ============================================================================
-- Event Registration
-- ============================================================================

onEvent("TickEvent", onTick)
onEvent("RenderEvent", onRender)
onEvent("LoadEvent", onLoad)
onEvent("EnableEvent", onEnable)
onEvent("DisableEvent", onDisable)