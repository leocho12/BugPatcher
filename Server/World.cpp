#include "World.h"
#include <algorithm>
#include <cmath>

constexpr float PLAYER_SPEED = 250.0f;// px/초
constexpr float PLAYER_STEP = PLAYER_SPEED * TICK_MS / 1000.0f; // 한 틱당 이동량

constexpr float ENEMY_SPEED = 62.5f;// px/초 (원본 16ms당 1px)
constexpr float ENEMY_FAST_SPEED = 125.0f;// px/초 (원본 16ms당 2px)
constexpr uint32_t SPAWN_INTERVAL_MS = 1000;// 적 생성 주기
constexpr int SPAWN_MAX_PER_WAVE = 10;// 한 번에 만드는 최대 수
constexpr int SPAWN_MARGIN = 32;// 화면 바깥으로 얼마나 떨어져서 나오는지
constexpr size_t MAX_ENEMIES = 500;// 스냅샷 크기와 서버 부하 보호용 상한
constexpr uint32_t CONTACT_INTERVAL_MS = 100;// 원본 충돌 타이머(100ms)와 같은 주기
constexpr float CONTACT_DIST = 32.0f;// 이 거리 안이면 닿은 것 (원본 CheckCollision)
constexpr int CONTACT_DAMAGE = 1;// 닿은 적 1마리당 피해

namespace {
	struct IPoint { int x, y; };

	// 원작 BugPather.cpp의 PointInPolygon / RectPolygonCollision을 그대로 옮김 (좌표를 int로 자르는 것까지 같다)
	bool PointInPolygon(const IPoint* poly, int n, float x, float y) {
		int count = 0;
		for (int i = 0; i < n; ++i) {
			IPoint p1 = poly[i];
			IPoint p2 = poly[(i + 1) % n];
			if ((p1.y > y) != (p2.y > y)) {
				float atX = (float)(p2.x - p1.x) * (y - p1.y) / (p2.y - p1.y) + p1.x;
				if (x < atX)count++;
			}
		}
		return (count % 2) == 1;
	}

	bool RectPolygonCollision(const IPoint* poly, int polyCount, float rectX, float rectY, float rectW, float rectH) {
		// 1. 적 사각형의 4점이 무기 폴리곤 안에 있는지
		IPoint rectPts[4] = {
			{ (int)rectX, (int)rectY },
			{ (int)(rectX + rectW), (int)rectY },
			{ (int)(rectX + rectW), (int)(rectY + rectH) },
			{ (int)rectX, (int)(rectY + rectH) },
		};
		for (int i = 0; i < 4; i++)
			if (PointInPolygon(poly, polyCount, (float)rectPts[i].x, (float)rectPts[i].y))return true;

		// 2. 무기 폴리곤의 점이 적 사각형 안에 있는지
		for (int i = 0; i < polyCount; i++) {
			float px = (float)poly[i].x;
			float py = (float)poly[i].y;
			if (px >= rectX && px <= rectX + rectW && py >= rectY && py <= rectY + rectH)return true;
		}
		return false;
	}

	// 쿨타임·지속 시간이 있는 무기(파리채, 에프킬라). 원작 weaponCoolTime과 같은 규칙이다.
	// 쿨타임이 지나면 켜지고 durationMs 동안 켜져 있으며, 켜져 있는 동안 tickMs가 지날 때마다 hit()를 부른다.
	// 33ms 틱 하나에 판정 시각이 여러 번 지나갈 수 있어(파리채 16ms) 지난 횟수만큼 부른다. 그래야 원작과 초당 피해가 같다
	template<class F>
	void StepWindow(int64_t now, WeaponState& w, const WeaponDef& d, F&& hit) {
		if (w.level == 0) { w.active = false; return; }
		if (!w.active && now - w.lastFireMs >= (int64_t)d.cooldownMs) {
			w.active = true;
			w.lastFireMs = now;
			w.lastTickMs = now;
		}
		if (!w.active)return;
		const int64_t until = (std::min)(now, w.lastFireMs + (int64_t)d.durationMs);// 켜져 있는 시간을 넘겨서 판정하지 않음
		while (w.lastTickMs + (int64_t)d.tickMs <= until) {
			w.lastTickMs += d.tickMs;
			hit();
		}
		if (now - w.lastFireMs >= (int64_t)d.durationMs)w.active = false;
	}

	// 항상 켜져 있는 무기(전기장, 홈매트). tickMs가 지날 때마다 hit()를 부른다
	template<class F>
	void StepAlways(int64_t now, WeaponState& w, const WeaponDef& d, F&& hit) {
		if (w.level == 0) { w.active = false; return; }
		w.active = true;
		while (w.lastTickMs + (int64_t)d.tickMs <= now) {
			w.lastTickMs += d.tickMs;
			hit();
		}
	}
}

void World::Init(EventQueue<RecvEvent>* rq, EventQueue<SendEvent>* sq,
	HANDLE ev, const GameStartInfo* info) {
	recvQ = rq;
	sendQ = sq;
	startEvent = ev;
	startInfo = info;
}

DWORD WINAPI World::ThreadMain(LPVOID self) {
	static_cast<World*>(self)->Run();// CreateThread용 정적 함수는 넘겨받기만
	return 0;
}

void World::OnInput(int playerId, const C2S_Input& pkt) {
	if (playerId < 0 || playerId >= MAX_PLAYERS) return;
	ServerPlayer& p = players[playerId];
	if (pkt.seq <= p.lastseq)return;// UDP를 쓰면 패킷이 늦게 도착할 수 있으므로 시퀀스 번호를 보고 이전 패킷이면 무시
	p.lastseq = pkt.seq;
	p.lastkeys = pkt.keys;
	if (std::isfinite(pkt.aimAngle))p.aimAngle = pkt.aimAngle;// 이상한 값(NaN 등)이 오면 이전 조준을 유지
}

void World::OnSelectWeapon(int,const C2S_SelectWeapon&){}
void World::OnDisconnect(int playerId) {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return;
	players[playerId].active = false;
	players[playerId].hp = 0;	// 나간 플레이어는 사망 처리
}

void World::SpawnPlayer() {
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		players[i] = ServerPlayer{};
		players[i].active = true;
		players[i].x = MAP_WIDTH / 2.0f + (i == 0 ? -50.0f : 50.0f);// 겹치지 않게 맵 중앙의 좌우에서 스폰
		players[i].y = MAP_HEIGHT / 2.0f;
		ResetWeapons(players[i]);
	}
	// 이전 판의 적이 남지 않게 초기화
	enemies.clear();
	nextEnemyId = 1;
	lastSpawnMs = nowMs;
	lastContactMs = nowMs;
}

void World::UpdatePlayers() {
	for (ServerPlayer& p:players) {
		if (!p.active || p.hp <= 0)continue;
		if (p.lastkeys & KEY_A)p.x -= PLAYER_STEP;
		if (p.lastkeys & KEY_D)p.x += PLAYER_STEP;
		if (p.lastkeys & KEY_W)p.y -= PLAYER_STEP;
		if (p.lastkeys & KEY_S)p.y += PLAYER_STEP;

		// 맵 경계 체크
		p.x = std::clamp(p.x, 0.0f, (float)MAP_WIDTH);
		p.y = std::clamp(p.y, 0.0f, (float)MAP_HEIGHT);
	}
}

void World::SpawnEnemiesPerTick() {
	if (nowMs - lastSpawnMs < SPAWN_INTERVAL_MS)return;// 1초가 안 지났으면 이번 틱은 0마리
	lastSpawnMs = nowMs;

	int count = 1 + (int)(nowMs / 1000 / 10);// 10초마다 1마리씩 늘어남
	count = (std::min)(count, SPAWN_MAX_PER_WAVE);
	for (int i = 0; i < count && enemies.size() < MAX_ENEMIES; ++i)
		SpawnEnemy();
}

void World::SpawnEnemy() {
	// 살아 있는 플레이어 중 한 명을 골라 그 화면 바깥에서 생성
	int alive[MAX_PLAYERS];
	int n = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i)
		if (players[i].active && players[i].hp > 0)alive[n++] = i;
	if (n == 0)return;
	const ServerPlayer& p = players[alive[rng() % n]];

	// 그 플레이어 화면의 왼쪽 위 좌표 (클라 카메라처럼 맵 밖으로 나가지 않게 자름)
	float camX = std::clamp(p.x - VIEW_WIDTH / 2.0f, 0.0f, (float)(MAP_WIDTH - VIEW_WIDTH));
	float camY = std::clamp(p.y - VIEW_HEIGHT / 2.0f, 0.0f, (float)(MAP_HEIGHT - VIEW_HEIGHT));

	ServerEnemy e;
	e.id = nextEnemyId++;
	if (nextEnemyId == 0)nextEnemyId = 1;// 65535 다음에 0을 건너뜀

	switch (rng() % 4) {
	case 0:// 위
		e.x = camX + rng() % VIEW_WIDTH;
		e.y = camY - SPAWN_MARGIN;
		break;
	case 1:// 아래
		e.x = camX + rng() % VIEW_WIDTH;
		e.y = camY + VIEW_HEIGHT + SPAWN_MARGIN;
		break;
	case 2:// 왼쪽
		e.x = camX - SPAWN_MARGIN;
		e.y = camY + rng() % VIEW_HEIGHT;
		break;
	case 3:// 오른쪽
		e.x = camX + VIEW_WIDTH + SPAWN_MARGIN;
		e.y = camY + rng() % VIEW_HEIGHT;
		break;
	}

	// 원본과 같은 확률: 빠른 적 20%, 나머지 중 40%가 탱크(전체의 32%), 그 외 일반
	if (rng() % 100 < 20) {
		e.type = ENEMY_FAST;
		e.hp = 10;
	}
	else if (rng() % 100 < 40) {
		e.type = ENEMY_TANK;
		e.hp = 20;
	}
	else {
		e.type = ENEMY_NORMAL;
		e.hp = 10;
	}

	enemies.push_back(e);
}

const ServerPlayer* World::PickTarget(const ServerEnemy& e) const {
	const ServerPlayer* best = nullptr;
	float bestDistSq = 0;
	for (const ServerPlayer& p : players) {
		if (!p.active || p.hp <= 0)continue;// 나갔거나 죽은 플레이어는 쫓지 않음
		float dx = p.x - e.x;
		float dy = p.y - e.y;
		float distSq = dx * dx + dy * dy;// 크기 비교만 하므로 sqrt 생략
		if (!best || distSq < bestDistSq) {
			best = &p;
			bestDistSq = distSq;
		}
	}
	return best;
}

void World::UpdateEnemies() {
	for (ServerEnemy& e : enemies) {
		const ServerPlayer* t = PickTarget(e);
		if (!t)continue;// 쫓아갈 플레이어가 없으면 멈춤
		float dx = t->x - e.x;
		float dy = t->y - e.y;
		float len = sqrtf(dx * dx + dy * dy);
		if (len <= 0.0f)continue;

		float speed = (e.type == ENEMY_FAST) ? ENEMY_FAST_SPEED : ENEMY_SPEED;
		if (nowMs < e.slowUntil)speed *= 0.5f;// 슬로우 상태
		float step = speed * TICK_MS / 1000.0f;// 한 틱당 이동량
		if (step > len)step = len;// 플레이어를 지나쳐서 앞뒤로 떨리지 않게
		e.x += dx / len * step;
		e.y += dy / len * step;
	}
}

void World::UpdateContactDamage() {
	if (nowMs - lastContactMs < CONTACT_INTERVAL_MS)return;
	lastContactMs += CONTACT_INTERVAL_MS;// = nowMs로 하면 33ms 틱 때문에 132ms마다 판정돼 피해가 줄어듦

	for (ServerPlayer& p : players) {
		if (!p.active || p.hp <= 0)continue;
		int hits = 0;
		for (const ServerEnemy& e : enemies) {
			float dx = p.x - e.x;
			float dy = p.y - e.y;
			if (dx * dx + dy * dy < CONTACT_DIST * CONTACT_DIST)++hits;
		}
		p.hp -= hits * CONTACT_DAMAGE;
		if (p.hp < 0)p.hp = 0;// 사망·관전 처리는 12/3
	}
}

void World::ResetWeapons(ServerPlayer& p) {
	for (int w = 0; w < WEAPON_COUNT; ++w) {
		p.weapons[w] = WeaponState{};
		p.weapons[w].lastFireMs = (int64_t)nowMs - (int64_t)WEAPON_DEFS[w].cooldownMs;// 얻자마자 발동할 수 있다 (원작은 lastAttackTime = 0)
		p.weapons[w].lastTickMs = (int64_t)nowMs;
	}
	p.weapons[WEAPON_SLASH].level = 1;// 파리채는 처음부터 가지고 시작한다
	p.homing = HomingState{};
}

void World::GrantWeapon(int playerId, int weaponType) {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return;
	if (weaponType < 0 || weaponType >= WEAPON_COUNT)return;
	ServerPlayer& p = players[playerId];
	WeaponState& w = p.weapons[weaponType];
	if (w.level == 0) {// 처음 얻음: 바로 발동할 수 있게 시각을 맞춘다
		w.active = false;
		w.lastFireMs = (int64_t)nowMs - (int64_t)WEAPON_DEFS[weaponType].cooldownMs;
		w.lastTickMs = (int64_t)nowMs;
		if (weaponType == WEAPON_HOMING)p.homing = HomingState{};
	}
	if (w.level < 255)++w.level;// 강화. 홈매트는 원작에서도 강화 효과가 없지만 레벨 표시는 올린다
}

void World::DamageEnemy(ServerEnemy& e, int damage, int playerId) {
	if (damage <= 0)return;
	e.hp -= damage;
	e.lastHitBy = (int8_t)playerId;
}

void World::ReapDeadEnemies() {
	bool any = false;
	for (const ServerEnemy& e : enemies) {
		if (e.hp > 0)continue;
		any = true;
		if (e.lastHitBy >= 0 && e.lastHitBy < MAX_PLAYERS){
			++players[e.lastHitBy].killCount;// 킬은 마지막으로 때린 플레이어에게
			OnKill(e.lastHitBy);
		}
	}
	if (!any)return;
	enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const ServerEnemy& e) { return e.hp <= 0; }), enemies.end());
}

void World::UpdateWeapons() {
	for (int pid = 0; pid < MAX_PLAYERS; ++pid) {
		ServerPlayer& p = players[pid];
		if (!p.active || p.hp <= 0) {// 나갔거나 죽은 플레이어의 무기는 멈춘다
			p.homing.active = false;
			for (WeaponState& w : p.weapons)w.active = false;
			continue;
		}
		UpdateSlash(pid);
		UpdateCircle(pid);
		UpdateBreath(pid);
		UpdateSlow(pid);
		UpdateHoming(pid);
		ReapDeadEnemies();// 한 플레이어의 무기가 끝날 때마다 정리한다. 이미 죽은 적을 다음 플레이어가 또 때리지 않게
	}
}

void World::UpdateSlash(int pid) {
	ServerPlayer& p = players[pid];
	WeaponState& w = p.weapons[WEAPON_SLASH];
	StepWindow((int64_t)nowMs, w, WEAPON_DEFS[WEAPON_SLASH], [&]() {
		const float ang = p.aimAngle;
		const float cx = p.x + cosf(ang) * SLASH_OFFSET;// 사각형 중심은 플레이어 앞쪽
		const float cy = p.y + sinf(ang) * SLASH_OFFSET;
		IPoint poly[4];
		for (int i = 0; i < 4; ++i) {
			const float x = (i < 2 ? -SLASH_WIDTH / 2.0f : SLASH_WIDTH / 2.0f);
			const float y = (i % 3 == 0 ? -SLASH_HEIGHT / 2.0f : SLASH_HEIGHT / 2.0f);
			poly[i].x = (int)(cx + x * cosf(ang) - y * sinf(ang));
			poly[i].y = (int)(cy + x * sinf(ang) + y * cosf(ang));
		}
		const int dmg = WeaponDamage(WEAPON_SLASH, w.level);
		for (ServerEnemy& e : enemies) {
			if (e.hp <= 0)continue;
			if (RectPolygonCollision(poly, 4, e.x - SLASH_ENEMY_BOX_OFFSET, e.y - SLASH_ENEMY_BOX_OFFSET, SLASH_ENEMY_BOX_SIZE, SLASH_ENEMY_BOX_SIZE))
				DamageEnemy(e, dmg, pid);
		}
	});
}

void World::UpdateCircle(int pid) {
	ServerPlayer& p = players[pid];
	WeaponState& w = p.weapons[WEAPON_CIRCLE];
	StepAlways((int64_t)nowMs, w, WEAPON_DEFS[WEAPON_CIRCLE], [&]() {
		const int dmg = WeaponDamage(WEAPON_CIRCLE, w.level);
		for (ServerEnemy& e : enemies) {
			if (e.hp <= 0)continue;
			const float dx = e.x - p.x;
			const float dy = e.y - p.y;
			if (dx * dx + dy * dy <= CIRCLE_RADIUS * CIRCLE_RADIUS)DamageEnemy(e, dmg, pid);
		}
	});
}

void World::UpdateBreath(int pid) {
	ServerPlayer& p = players[pid];
	WeaponState& w = p.weapons[WEAPON_BREATH];
	StepWindow((int64_t)nowMs, w, WEAPON_DEFS[WEAPON_BREATH], [&]() {
		const int dmg = WeaponDamage(WEAPON_BREATH, w.level);
		for (ServerEnemy& e : enemies) {
			if (e.hp <= 0)continue;
			const float dx = e.x - p.x;
			const float dy = e.y - p.y;
			if (dx * dx + dy * dy > BREATH_RADIUS * BREATH_RADIUS)continue;
			// 조준 방향과 적 방향의 각도 차이 (0 ~ PI)
			float diff = fabsf(fmodf(p.aimAngle - atan2f(dy, dx), 2.0f * PI_F));
			if (diff > PI_F)diff = 2.0f * PI_F - diff;
			if (diff <= BREATH_HALF_ANGLE)DamageEnemy(e, dmg, pid);
		}
	});
}

void World::UpdateSlow(int pid) {
	ServerPlayer& p = players[pid];
	WeaponState& w = p.weapons[WEAPON_SLOW];
	StepAlways((int64_t)nowMs, w, WEAPON_DEFS[WEAPON_SLOW], [&]() {
		for (ServerEnemy& e : enemies) {
			const float dx = e.x - p.x;
			const float dy = e.y - p.y;
			if (dx * dx + dy * dy <= SLOW_RADIUS * SLOW_RADIUS)e.slowUntil = nowMs + SLOW_HOLD_MS;
		}
	});
}

int World::FindHomingTarget(const ServerPlayer& p) const {
	// 그 플레이어 화면 안의 적만 노린다 (클라 카메라와 같은 계산)
	const float camX = std::clamp(p.x - VIEW_WIDTH / 2.0f, 0.0f, (float)(MAP_WIDTH - VIEW_WIDTH));
	const float camY = std::clamp(p.y - VIEW_HEIGHT / 2.0f, 0.0f, (float)(MAP_HEIGHT - VIEW_HEIGHT));
	int best = -1;
	int maxHp = -1;
	float minDist = 0;
	for (int i = 0; i < (int)enemies.size(); ++i) {
		const ServerEnemy& e = enemies[i];
		if (e.hp <= 0)continue;
		if (e.x < camX || e.x > camX + VIEW_WIDTH || e.y < camY || e.y > camY + VIEW_HEIGHT)continue;
		const float dist = sqrtf((p.x - e.x) * (p.x - e.x) + (p.y - e.y) * (p.y - e.y));
		if (e.hp > maxHp) {// HP가 가장 높은 적
			maxHp = e.hp;
			best = i;
			minDist = dist;
		}
		else if (e.hp == maxHp && dist < minDist) {// 같으면 가까운 적
			best = i;
			minDist = dist;
		}
	}
	return best;
}

void World::UpdateHoming(int pid) {
	ServerPlayer& p = players[pid];
	WeaponState& w = p.weapons[WEAPON_HOMING];
	HomingState& h = p.homing;
	const WeaponDef& d = WEAPON_DEFS[WEAPON_HOMING];
	if (w.level == 0) { h.active = false; return; }

	// 쿨타임이 지났고 노릴 적이 있으면 발사. 쿨타임은 발사한 시각부터 센다
	if (!h.active && (int64_t)nowMs - w.lastFireMs >= (int64_t)d.cooldownMs) {
		const int idx = FindHomingTarget(p);
		if (idx >= 0) {
			h.active = true;
			h.targetId = enemies[idx].id;
			h.x = p.x;
			h.y = p.y;
			w.lastFireMs = (int64_t)nowMs;
		}
	}
	if (!h.active)return;

	ServerEnemy* target = nullptr;
	for (ServerEnemy& e : enemies)
		if (e.id == h.targetId && e.hp > 0) { target = &e; break; }
	// 시간이 지났거나, 쫓던 적이 이미 다른 무기에 죽었으면 사라진다
	if (!target || (int64_t)nowMs - w.lastFireMs >= (int64_t)d.durationMs) { h.active = false; return; }

	const float dx = target->x - h.x;
	const float dy = target->y - h.y;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len > 0.1f) {
		float step = HOMING_SPEED * TICK_MS / 1000.0f;
		if (step > len)step = len;// 적을 지나쳐서 못 맞히는 일이 없게 (원작은 지나칠 수 있었다)
		h.x += dx / len * step;
		h.y += dy / len * step;
	}
	const float ex = target->x - h.x;
	const float ey = target->y - h.y;
	if (ex * ex + ey * ey <= HOMING_HIT_RADIUS * HOMING_HIT_RADIUS) {
		DamageEnemy(*target, WeaponDamage(WEAPON_HOMING, w.level), pid);
		h.active = false;
	}
}

void World::Run() {
	WaitForSingleObject(startEvent, INFINITE);// 게임 시작 신호를 기다림
	if (!running)return;// 시작전에 서버가 꺼지면 종료

	SpawnPlayer();
BroadcastGameStart();// 시작 위치를 정한 직후에 클라에 알림

	ULONGLONG nextTick = GetTickCount64();
	while (running) {
		ULONGLONG now = GetTickCount64();
		if (now < nextTick) {
			Sleep((DWORD)(nextTick - now));// 다음 틱까지 잠
			continue;
		}
		nextTick += TICK_MS;// now가 아니라 예정 시각에 더해야 오차가 쌓이지 않음

		ProcessRecvEvents();
		Update(TICK_MS);
	}
}

void World::Update(uint32_t dtMs) {
	nowMs += dtMs;
	++tick;
	UpdatePlayers();
	if (weaponsEnabled)UpdateWeapons();// 계획서 순서: 플레이어 이동 → 무기 처리 → 적 생성·이동 → 충돌·피해
	SpawnEnemiesPerTick();
	UpdateEnemies();
	UpdateContactDamage();
	SendSnapshot();
}

void World::SendSnapshot() {
	PlayerState ps[MAX_PLAYERS]{};
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		const ServerPlayer& p = players[i];
		ps[i].id = (uint8_t)i;
		ps[i].x = (int16_t)lroundf(p.x);
		ps[i].y = (int16_t)lroundf(p.y);
		ps[i].hp = (int16_t)p.hp;
		ps[i].level = 1;// 레벨업은 11/25
		ps[i].killCount = (uint16_t)(std::min)(p.killCount, 65535);
		ps[i].aimAngle = p.aimAngle;
		for (int w = 0; w < WEAPON_COUNT; ++w) {
			ps[i].weaponLevel[w] = p.weapons[w].level;
			if (p.weapons[w].active)ps[i].weaponActiveBits |= (uint8_t)(1u << w);// 지금 이펙트를 보여야 하는 무기
		}
		ps[i].homingActive = p.homing.active ? 1 : 0;
		ps[i].homingX = (int16_t)lroundf(p.homing.x);
		ps[i].homingY = (int16_t)lroundf(p.homing.y);
	}

	// 적을 보낼 형태로 변환
	std::vector<EnemyState> es;
	es.reserve(enemies.size());
	for (const ServerEnemy& e : enemies) {
		EnemyState s{};
		s.id = e.id;
		s.type = e.type;
		s.x = (int16_t)lroundf(e.x);
		s.y = (int16_t)lroundf(e.y);
		es.push_back(s);
	}

	// 조각 하나에 들어가는 적 수 = (1400 - 고정 부분) / 적 1마리 크기
	const size_t fixedBytes = sizeof(S2C_SnapshotHead) + sizeof(ps) + sizeof(uint16_t);// 머리 + 플레이어 + 적 수
	const size_t extra0 = sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint8_t);// 0번 조각만: 보스, 탄막, 기믹 수
	const size_t perFrag0 = (MAX_UDP_SIZE - fixedBytes - extra0) / sizeof(EnemyState);
	const size_t perFrag = (MAX_UDP_SIZE - fixedBytes) / sizeof(EnemyState);

	size_t fragCount = 1;
	if (es.size() > perFrag0)
		fragCount += (es.size() - perFrag0 + perFrag - 1) / perFrag;// 남은 적을 올림 나눗셈

	size_t next = 0;// 다음에 담을 적 번호
	for (size_t f = 0; f < fragCount; ++f) {
		std::vector<char> buf(sizeof(S2C_SnapshotHead));// 헤더 자리만 먼저 잡음
		auto put = [&](const void* p, size_t n) {
			const char* c = static_cast<const char*>(p);
			buf.insert(buf.end(), c, c + n);
		};

		put(ps, sizeof(ps));// 모든 조각에 같은 플레이어 정보
		if (f == 0) {// 0번 조각만 보스, 탄막, 기믹 (지금은 전부 0개)
			uint8_t bossCount = 0;		put(&bossCount, sizeof(bossCount));
			uint16_t bulletCount = 0;	put(&bulletCount, sizeof(bulletCount));
			uint8_t gimmickCount = 0;	put(&gimmickCount, sizeof(gimmickCount));
		}

		size_t room = (f == 0) ? perFrag0 : perFrag;
		uint16_t n = (uint16_t)(std::min)(es.size() - next, room);
		put(&n, sizeof(n));
		if (n > 0)put(&es[next], n * sizeof(EnemyState));
		next += n;

		// insert 중에 버퍼가 다시 할당될 수 있으므로 헤더는 마지막에 채움
		S2C_SnapshotHead* h = reinterpret_cast<S2C_SnapshotHead*>(buf.data());
		h->header.size = (uint16_t)buf.size();
		h->header.type = S2C_SNAPSHOT;
		h->tick = tick;
		h->fragIndex = (uint8_t)f;
		h->fragCount = (uint8_t)fragCount;

		sendQ->Push(SendEvent{ TARGET_ALL, true, std::move(buf) });
	}
}

void World::BroadcastGameStart() {
	S2C_GameStart pkt{};
	InitPacket(pkt, S2C_GAME_START);
	pkt.mapId = startInfo->mapId;
	for (int i = 0;i < MAX_PLAYERS;++i) {
		pkt.startX[i] = (int16_t)lroundf(players[i].x);
		pkt.startY[i] = (int16_t)lroundf(players[i].y);
	}
	sendQ->Push(SendEvent{ TARGET_ALL,false,ToBytes(pkt) });// false=TCP
}

void World::ProcessRecvEvents() {
	recvQ->SwapOut(recvBuf);// 쓰레드가 쌓아논 이벤트 가져옴

	for (const RecvEvent& e : recvBuf) {// 이밴트 하나씩 꺼내기
		if (e.kind == RecvKind::Disconnect) {
			OnDisconnect(e.playerId);
			continue;
		}
		if (e.data.size() < sizeof(PacketHeader))continue;// 너무 작은 패킷은 무시
		
		const PacketHeader* h = reinterpret_cast<const PacketHeader*>(e.data.data());// 패킷 헤더를 꺼내서 타입 확인
		switch (h->type) {
		case C2S_INPUT:// 키 입력 패킷인 경우
			if (e.data.size() == sizeof(C2S_Input))
				OnInput(e.playerId, *reinterpret_cast<const C2S_Input*>(h));
			break;
		case C2S_SELECT_WEAPON:// 무기 선택 패킷인 경우
			if (e.data.size() == sizeof(C2S_SelectWeapon))
				OnSelectWeapon(e.playerId, *reinterpret_cast<const C2S_SelectWeapon*>(h));
			break;
		}
	}
}

void World::OnKill(int pid) {
	ServerPlayer& p = players[pid];
	if (!p.active)return;
	if (p.level + p.pendingLevelUps >= MAX_LEVEL)return;//이미 만렙이거나 만렙 예정
	if (++p.levelKills >= p.level * 5) {
		p.levelKills = 0;
		++p.pendingLevelUps;
		TryStartLevelUp(pid);
	}
}