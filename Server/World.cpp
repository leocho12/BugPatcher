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

void World::OnSelectWeapon(int pid,const C2S_SelectWeapon& pkt){
	if (pid < 0 || pid >= MAX_PLAYERS)return;
	ServerPlayer& p = players[pid];
	if (!p.active || !p.choosing)return;// 나갔거나 죽은 플레이어는 무기 선택 못함
	if (pkt.cardIndex >= LEVELUP_CHOICES)return;
	GrantWeapon(pid, p.choices[pkt.cardIndex]);
	p.choosing = false;
	TryStartLevelUp(pid);// 대기중인 레벨업이 있으면 다음 카드 선택 시작
}
void World::OnDisconnect(int playerId) {
	if (playerId < 0 || playerId >= MAX_PLAYERS)return;
	players[playerId].active = false;
	players[playerId].hp = 0;	// 나간 플레이어는 사망 처리
	players[playerId].choosing = false;
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
	for (ServerBoss& b : bosses)b = ServerBoss{};// 새 판은 보스도 처음부터
	bullets.clear();
	gimmicks.clear();
	lastCDSpawnMs = lastFanSpawnMs = nowMs;// 첫 기믹은 시작 8초 뒤
	nextEnemyId = 1;
	lastSpawnMs = nowMs;
	lastContactMs = nowMs;
	finished = false;
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
	return PickTargetAt(e.x, e.y);
}

const ServerPlayer* World::PickTargetAt(float x, float y) const {
	const ServerPlayer* best = nullptr;
	float bestDistSq = 0;
	for (const ServerPlayer& p : players) {
		if (!p.active || p.hp <= 0)continue;// 나갔거나 죽은 플레이어는 쫓지 않음
		float dx = p.x - x;
		float dy = p.y - y;
		float distSq = dx * dx + dy * dy;
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
		if (p.choosing)continue;// 레벨업 카드를 고르는 동안은 무적
		int hits = 0;
		for (const ServerEnemy& e : enemies) {
			float dx = p.x - e.x;
			float dy = p.y - e.y;
			if (dx * dx + dy * dy < CONTACT_DIST * CONTACT_DIST)++hits;
		}
		int bossDamage = 0;
		for (const ServerBoss& b : bosses) {
			if (!b.active)continue;
			const float dx = p.x - b.x, dy = p.y - b.y;
			const float reach = BOSS_RADIUS + PLAYER_RADIUS;
			if (dx * dx + dy * dy <= reach * reach)bossDamage += BOSS_DEFS[b.type].contactDamage;
		}
		p.hp -= hits * CONTACT_DAMAGE + bossDamage;
		if (p.hp < 0)p.hp = 0;// 사망·관전 처리는
	}
}

void World::ResetWeapons(ServerPlayer& p) {
	for (int w = 0; w < WEAPON_COUNT; ++w) {
		p.weapons[w] = WeaponState{};
		p.weapons[w].lastFireMs = (int64_t)nowMs - (int64_t)WEAPON_DEFS[w].cooldownMs;// 얻자마자 발동
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
	if (w.level == 0) {
		w.active = false;
		w.lastFireMs = (int64_t)nowMs - (int64_t)WEAPON_DEFS[weaponType].cooldownMs;
		w.lastTickMs = (int64_t)nowMs;
		if (weaponType == WEAPON_HOMING)p.homing = HomingState{};
	}
	if (w.level < 255)++w.level;// 강화
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
		if (p.choosing) {// 레벨업 카드를 고르는 동안은 공격하지 않는다 (소금총 탄도 사라진다)
			p.homing.active = false;
			for (WeaponState& w : p.weapons) {
				w.active = false;
				w.lastTickMs = (int64_t)nowMs;// 끝난 뒤 밀린 판정이 한꺼번에 들어가지 않게 판정 시각을 맞춰 둔다
			}
			continue;
		}
		UpdateSlash(pid);
		UpdateCircle(pid);
		UpdateBreath(pid);
		UpdateSlow(pid);
		UpdateHoming(pid);
		ReapDeadEnemies();// 한 플레이어의 무기가 끝날 때마다 정리 이미 죽은 적을 다음 플레이어가 또 때리지 않게
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
		for (ServerBoss& b : bosses) {
			if (!b.active || b.hp <= 0)continue;
			if (RectPolygonCollision(poly, 4, b.x - BOSS_RADIUS, b.y - BOSS_RADIUS, BOSS_RADIUS * 2, BOSS_RADIUS * 2))
				DamageBoss(b, dmg);
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
		for (ServerBoss& b : bosses) {
			if (!b.active || b.hp <= 0)continue;
			const float dx = b.x - p.x;
			const float dy = b.y - p.y;
			if (dx * dx + dy * dy <= CIRCLE_RADIUS * CIRCLE_RADIUS)DamageBoss(b, dmg);
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
		for (ServerBoss& b : bosses) {
			if (!b.active || b.hp <= 0)continue;
			const float dx = b.x - p.x;
			const float dy = b.y - p.y;
			const float reach = BREATH_RADIUS + BOSS_RADIUS;
			if (dx * dx + dy * dy > reach * reach)continue;
			float diff = fabsf(fmodf(p.aimAngle - atan2f(dy, dx), 2.0f * PI_F));
			if (diff > PI_F)diff = 2.0f * PI_F - diff;
			if (diff <= BREATH_HALF_ANGLE)DamageBoss(b, dmg);
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

int World::FindHomingBoss(const ServerPlayer& p) const {
	const float camX = std::clamp(p.x - VIEW_WIDTH / 2.0f, 0.0f, (float)(MAP_WIDTH - VIEW_WIDTH));
	const float camY = std::clamp(p.y - VIEW_HEIGHT / 2.0f, 0.0f, (float)(MAP_HEIGHT - VIEW_HEIGHT));
	int best = -1;
	int maxHp = -1;
	for (int i = 0; i < 3; ++i) {
		const ServerBoss& b = bosses[i];
		if (!b.active || b.hp <= 0)continue;
		if (b.x < camX || b.x > camX + VIEW_WIDTH || b.y < camY || b.y > camY + VIEW_HEIGHT)continue;
		if (b.hp > maxHp) {
			maxHp = b.hp;
			best = i;
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

	// 쿨타임이 지났고 노릴 대상이 있으면 발사. 화면 안에 보스가 있으면 보스를 먼저 노린다
	if (!h.active && (int64_t)nowMs - w.lastFireMs >= (int64_t)d.cooldownMs) {
		const int bossIdx = FindHomingBoss(p);
		const int idx = (bossIdx >= 0) ? -1 : FindHomingTarget(p);
		if (bossIdx >= 0 || idx >= 0) {
			h.active = true;
			h.bossIdx = (int8_t)bossIdx;// 보스가 없으면 -1
			h.targetId = (bossIdx >= 0) ? 0 : enemies[idx].id;
			h.x = p.x;
			h.y = p.y;
			w.lastFireMs = (int64_t)nowMs;
		}
	}
	if (!h.active)return;

	// 쫓는 대상을 찾는다 (보스 또는 일반 적)
	ServerEnemy* target = nullptr;
	ServerBoss* bossTarget = nullptr;
	if (h.bossIdx >= 0) {
		ServerBoss& b = bosses[h.bossIdx];
		if (b.active && b.hp > 0)bossTarget = &b;
	}
	else {
		for (ServerEnemy& e : enemies)
			if (e.id == h.targetId && e.hp > 0) { target = &e; break; }
	}
	// 시간이 지났거나, 쫓던 대상이 이미 다른 무기에 죽었으면 사라진다
	if ((!target && !bossTarget) || (int64_t)nowMs - w.lastFireMs >= (int64_t)d.durationMs) { h.active = false; return; }

	const float tx = bossTarget ? bossTarget->x : target->x;
	const float ty = bossTarget ? bossTarget->y : target->y;
	const float dx = tx - h.x;
	const float dy = ty - h.y;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len > 0.1f) {
		float step = HOMING_SPEED * TICK_MS / 1000.0f;
		if (step > len)step = len;// 대상을 지나쳐서 못 맞히는 일이 없게 (원작은 지나칠 수 있었다)
		h.x += dx / len * step;
		h.y += dy / len * step;
	}
	// 보스는 몸집이 커서(반지름 64) 중심이 아니라 가장자리에 닿으면 맞은 것으로 한다
	const float hitR = HOMING_HIT_RADIUS + (bossTarget ? BOSS_RADIUS : 0.0f);
	const float ex = tx - h.x;
	const float ey = ty - h.y;
	if (ex * ex + ey * ey <= hitR * hitR) {
		const int dmg = WeaponDamage(WEAPON_HOMING, w.level);
		if (bossTarget)DamageBoss(*bossTarget, dmg);
		else DamageEnemy(*target, dmg, pid);
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
	if (finished)return;
	nowMs += dtMs;
	++tick;
	UpdatePlayers();
	if (weaponsEnabled)UpdateWeapons();
	ReapDeadBosses();// 무기로 죽은 보스 처리 (무기가 꺼져 있어도 부름)
	SpawnEnemiesPerTick();
	UpdateEnemies();
	SpawnBosses();
	UpdateBosses();
	UpdateBossBullets();
	CheckGimmick();
	UpdateGimmicks();
	UpdateContactDamage();
	CheckGameResult();
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
		ps[i].level = (uint8_t)p.level;
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

	std::vector<BossState> bs;
	for (const ServerBoss& b : bosses) {
		if (!b.active)continue;
		BossState s{};
		s.type = b.type;
		s.x = (int16_t)lroundf(b.x);
		s.y = (int16_t)lroundf(b.y);
		s.hp = (int16_t)(std::max)(b.hp, 0);
		s.maxHp = (int16_t)b.maxHp;
		s.aoeStep = b.aoeStep;
		bs.push_back(s);
	}
	std::vector<BulletState> bl;
	bl.reserve(bullets.size());
	for (const ServerBullet& b : bullets) {
		BulletState s{};
		s.x = (int16_t)lroundf(b.x);
		s.y = (int16_t)lroundf(b.y);
		bl.push_back(s);
	}
	std::vector<GimmickState> gs;
	for (const ServerGimmick& g : gimmicks) {
		GimmickState s{};
		s.kind = g.kind;
		s.x = (int16_t)lroundf(g.x);
		s.y = (int16_t)lroundf(g.y);
		s.dirX = g.dirX;
		s.dirY = g.dirY;
		gs.push_back(s);
	}

	// 조각 하나에 들어가는 적 수 = (1400 - 고정 부분) / 적 1마리 크기
	const size_t fixedBytes = sizeof(S2C_SnapshotHead) + sizeof(ps) + sizeof(uint16_t);// 머리 + 플레이어 + 적 수
	const size_t extra0 = sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint8_t);// 0번 조각만: 보스, 탄막, 기믹 수
	const size_t perFrag0 = (MAX_UDP_SIZE - fixedBytes - extra0 - bs.size() * sizeof(BossState) - bl.size() * sizeof(BulletState) - gs.size() * sizeof(GimmickState)) / sizeof(EnemyState);
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
		if (f == 0) {// 0번 조각만 보스, 탄막, 기믹
			uint8_t bossCount = (uint8_t)bs.size();		put(&bossCount, sizeof(bossCount));
			if (!bs.empty())put(bs.data(), bs.size() * sizeof(BossState));
			uint16_t bulletCount = (uint16_t)bl.size();	put(&bulletCount, sizeof(bulletCount));
			if (!bl.empty())put(bl.data(), bl.size() * sizeof(BulletState));
			uint8_t gimmickCount = (uint8_t)gs.size();	put(&gimmickCount, sizeof(gimmickCount));
			if (!gs.empty())put(gs.data(), gs.size() * sizeof(GimmickState));
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
	if (!p.active || p.hp <= 0)return;// 죽은 플레이어는 레벨업하지 않도록
	if (p.level + p.pendingLevelUps >= MAX_LEVEL)return;//이미 만렙이거나 만렙 예정
	if (++p.levelKills >= (p.level+p.pendingLevelUps) * 5) {
		p.levelKills = 0;
		++p.pendingLevelUps;
		TryStartLevelUp(pid);
	}
}

void World::TryStartLevelUp(int pid) {
	ServerPlayer& p = players[pid];
	if (p.choosing || p.pendingLevelUps <= 0)return;
	--p.pendingLevelUps;
	++p.level;
	p.hp = PLAYER_MAX_HP;// 레벨업한 플레이어 체력 회복
	
	int pool[WEAPON_COUNT];
	for (int i = 0;i < WEAPON_COUNT;i++)pool[i] = i;
	std::shuffle(pool, pool + WEAPON_COUNT, rng);// 카드3장에 무기 중복으로 뜨지 않도록 셔플
	for (int i = 0;i < LEVELUP_CHOICES;++i) p.choices[i] = (uint8_t)pool[i];
	p.choosing = true;

	SendLevelUp(pid);
}

void World::SendLevelUp(int pid) {
	const ServerPlayer& p = players[pid];
	S2C_LevelUp pkt{};
	InitPacket(pkt, S2C_LEVEL_UP);
	for (int i = 0;i < LEVELUP_CHOICES;++i)pkt.choices[i] = p.choices[i];
	sendQ->Push(SendEvent{ (uint8_t)pid, false,ToBytes(pkt) });
}

namespace {
	// 선분 a->b 가 원(c, 반지름 r)에 닿는지. 보스2의 고속탄은 한 틱에 62px를 가서 지름(48px)보다 멀어 점 검사만 하면 지나쳐 버린다
	bool SegHitsCircle(float ax, float ay, float bx, float by, float cx, float cy, float r) {
		float dx = bx - ax, dy = by - ay;
		float len2 = dx * dx + dy * dy;
		float t = len2 > 0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0.0f;
		t = (std::max)(0.0f, (std::min)(1.0f, t));
		float px = ax + dx * t - cx, py = ay + dy * t - cy;
		return px * px + py * py <= r * r;
	}
}

void World::BroadcastBossAlert(uint8_t bossType) {
	S2C_BossAlert pkt{};
	InitPacket(pkt, S2C_BOSS_ALERT);
	pkt.bossType = bossType;
	sendQ->Push(SendEvent{ TARGET_ALL, false, ToBytes(pkt) });// false=TCP
}

void World::AddBullet(float x, float y, float vx, float vy) {
	if (bullets.size() >= MAX_BOSS_BULLETS)return;// 스냅샷 크기 보호
	ServerBullet b;
	b.x = x; b.y = y; b.vx = vx; b.vy = vy;
	bullets.push_back(b);
}

void World::FireRing(const ServerBoss& b, float speed) {
	for (int i = 0; i < RING_COUNT; ++i) {
		float angle = i * 2.0f * PI_F / RING_COUNT;
		AddBullet(b.x, b.y, cosf(angle) * speed, sinf(angle) * speed);
	}
}

void World::SpawnBosses() {
	if (!bossesEnabled)return;
	for (int type = BOSS_BASIC; type <= BOSS_DASH_AND_BULLET; ++type) {
		ServerBoss& b = bosses[type - 1];
		if (b.active || b.killed)continue;
		if (nowMs <= BOSS_DEFS[type].spawnAtMs)continue;
		const ServerPlayer* t = PickTargetAt(MAP_WIDTH / 2.0f, MAP_HEIGHT / 2.0f);
		if (!t)return;// 살아있는 플레이어가 없으면 나오지 않는다

		b = ServerBoss{};
		b.active = true;
		b.type = (uint8_t)type;
		b.x = (std::max)(0.0f, (std::min)((float)MAP_WIDTH, t->x + 200.0f));// 원작: 플레이어 +(200, -100)
		b.y = (std::max)(0.0f, (std::min)((float)MAP_HEIGHT, t->y - 100.0f));
		b.hp = b.maxHp = BOSS_DEFS[type].hp;
		b.lastAttackMs = b.lastDashMs = b.aoeLastMs = (int64_t)nowMs;// 첫 공격은 등장 후 주기가 지난 뒤
		BroadcastBossAlert((uint8_t)type);
	}
}

void World::UpdateBosses() {
	const float dt = TICK_MS / 1000.0f;
	const int64_t now = (int64_t)nowMs;
	for (ServerBoss& b : bosses) {
		if (!b.active)continue;
		const ServerPlayer* t = PickTargetAt(b.x, b.y);
		if (!t)continue;// 쫓을 플레이어가 없으면 모든 행동을 쉰다
		const BossDef& d = BOSS_DEFS[b.type];

		// 기본 이동: 가장 가까운 플레이어를 쫓는다
		const float dx = t->x - b.x, dy = t->y - b.y;
		const float len = sqrtf(dx * dx + dy * dy);
		if (len > 0.1f) {
			const float step = (std::min)(d.speed * dt, len);
			b.x += dx / len * step;
			b.y += dy / len * step;
		}

		// 탄막
		if (b.type == BOSS_BASIC && now - b.lastAttackMs >= (int64_t)RING_INTERVAL_1) {
			b.lastAttackMs = now;
			FireRing(b, RING_SPEED);// 8방향
		}
		if (b.type == BOSS_DASH_SHOOT && now - b.lastAttackMs >= (int64_t)AIM_INTERVAL) {
			b.lastAttackMs = now;
			if (len > 0.0f)AddBullet(b.x, b.y, dx / len * AIM_SPEED, dy / len * AIM_SPEED);// 플레이어 방향 1발
		}
		if (b.type == BOSS_DASH_AND_BULLET && now - b.lastAttackMs >= (int64_t)RING_INTERVAL_3) {
			b.lastAttackMs = now;
			FireRing(b, RING_SPEED);
		}

		// 돌진 (보스2, 보스3). 평소 이동에 더해서 움직인다
		if (b.type != BOSS_BASIC) {
			if (!b.dashing && now - b.lastDashMs >= (int64_t)DASH_INTERVAL) {
				b.lastDashMs = now;
				b.dashStartMs = now;
				b.dashing = true;
				if (len > 0.0f) { b.dashDx = dx / len; b.dashDy = dy / len; }
			}
			if (b.dashing) {
				b.x += b.dashDx * DASH_SPEED * dt;
				b.y += b.dashDy * DASH_SPEED * dt;
				if (now - b.dashStartMs >= (int64_t)DASH_DURATION)b.dashing = false;
			}
		}

		// 광역 공격 (보스3): 5초마다 3단계, 300ms 간격, 반경 150 -> 200 -> 250
		if (b.type == BOSS_DASH_AND_BULLET) {
			if (b.aoeStep == 0 && now - b.aoeLastMs >= (int64_t)AOE_COOLDOWN) {
				b.aoeStep = 1;
				b.aoeTickMs = now;
			}
			if (b.aoeStep > 0 && now - b.aoeTickMs >= (int64_t)AOE_STEP_MS) {
				b.aoeTickMs = now;
				const float range = (float)(AOE_BASE + (b.aoeStep - 1) * AOE_GROW);
				for (ServerPlayer& p : players) {
					if (!p.active || p.hp <= 0 || p.choosing)continue;// 레벨업 카드를 고르는 동안은 무적
					const float px = p.x - b.x, py = p.y - b.y;
					if (px * px + py * py <= range * range) {
						p.hp -= AOE_DAMAGE;
						if (p.hp < 0)p.hp = 0;
					}
				}
				if (++b.aoeStep > AOE_STEPS) {
					b.aoeStep = 0;
					b.aoeLastMs = now;
				}
			}
		}
	}
}

void World::UpdateBossBullets() {
	const float dt = TICK_MS / 1000.0f;
	const float hitR = PLAYER_RADIUS + BULLET_RADIUS;
	for (size_t i = 0; i < bullets.size(); ) {
		ServerBullet& b = bullets[i];
		const float px = b.x, py = b.y;// 이동 전 위치
		b.x += b.vx * dt;
		b.y += b.vy * dt;

		bool remove = false;
		for (ServerPlayer& p : players) {
			if (!p.active || p.hp <= 0 || p.choosing)continue;
			if (SegHitsCircle(px, py, b.x, b.y, p.x, p.y, hitR)) {
				p.hp -= BULLET_DAMAGE;
				if (p.hp < 0)p.hp = 0;
				remove = true;
				break;
			}
		}
		if (b.x < 0 || b.x > MAP_WIDTH || b.y < 0 || b.y > MAP_HEIGHT)remove = true;
		if (remove) {
			bullets[i] = bullets.back();// 순서는 상관없으니 마지막 것으로 덮어쓰고 줄인다
			bullets.pop_back();
		}
		else ++i;
	}
}

void World::DamageBoss(ServerBoss& b, int damage) {
	if (damage <= 0)return;
	b.hp -= damage;// 보스는 킬 수에 포함되지 않는다 (원작과 같음)
}

void World::ReapDeadBosses() {
	for (ServerBoss& b : bosses) {
		if (!b.active || b.hp > 0)continue;
		b.active = false;
		b.killed = true;
		b.dashing = false;
		b.aoeStep = 0;
		for (ServerPlayer& p : players)
			if (p.active && p.hp > 0)p.hp = PLAYER_MAX_HP;// 원작: 보스를 잡으면 HP 회복
	}
}

void World::CheckGimmick() {
	if (!gimmicksEnabled || startInfo->mapId != GIMMICK_MAP_ID)return;
	if (nowMs - lastCDSpawnMs >= CD_SPAWN_MS) {
		lastCDSpawnMs = nowMs;
		SpawnGimmick(GIMMICK_CD);
	}
	if (nowMs - lastFanSpawnMs >= FAN_SPAWN_MS) {
		lastFanSpawnMs = nowMs;
		SpawnGimmick(GIMMICK_FAN);
	}
}

void World::SpawnGimmick(uint8_t kind) {
	if (gimmicks.size() >= MAX_GIMMICKS)return;
	ServerGimmick g;
	g.kind = kind;
	g.spawnMs = nowMs;
	switch (rng() % 4) {// 한쪽 끝에서 반대쪽으로
	case 0:  g.x = (float)(rng() % MAP_WIDTH); g.y = 0;                 g.dirY = 1;  break;// 위 -> 아래
	case 1:  g.x = (float)(rng() % MAP_WIDTH); g.y = (float)MAP_HEIGHT; g.dirY = -1; break;// 아래 -> 위
	case 2:  g.x = 0;                g.y = (float)(rng() % MAP_HEIGHT); g.dirX = 1;  break;// 왼쪽 -> 오른쪽
	default: g.x = (float)MAP_WIDTH; g.y = (float)(rng() % MAP_HEIGHT); g.dirX = -1; break;// 오른쪽 -> 왼쪽
	}
	gimmicks.push_back(g);
}

void World::UpdateGimmicks() {
	const float dt = TICK_MS / 1000.0f;
	for (size_t i = 0; i < gimmicks.size(); ) {
		ServerGimmick& g = gimmicks[i];
		const bool isCd = (g.kind == GIMMICK_CD);
		const float speed = isCd ? CD_SPEED : FAN_MOVE_SPEED;
		g.x += g.dirX * speed * dt;
		g.y += g.dirY * speed * dt;

		const float margin = isCd ? CD_OUT_MARGIN : FAN_OUT_MARGIN;
		bool remove = g.x < -margin || g.x > MAP_WIDTH + margin || g.y < -margin || g.y > MAP_HEIGHT + margin;
		if (!isCd && nowMs - g.spawnMs >= FAN_DURATION_MS)remove = true;

		if (!remove && isCd) {// CD: 처음 닿은 플레이어 1명에게 피해를 주고 사라진다
			for (ServerPlayer& p : players) {
				if (!p.active || p.hp <= 0 || p.choosing)continue;
				const float dx = p.x - g.x, dy = p.y - g.y;
				if (dx * dx + dy * dy <= CD_RADIUS * CD_RADIUS) {
					p.hp -= CD_DAMAGE;
					if (p.hp < 0)p.hp = 0;
					remove = true;
					break;
				}
			}
		}
		else if (!remove) {// 바람: 범위 안의 플레이어와 적을 바람 방향으로 민다
			auto inWind = [&](float x, float y) {
				const float along = (g.dirX != 0) ? fabsf(x - g.x) : fabsf(y - g.y);
				const float across = (g.dirX != 0) ? fabsf(y - g.y) : fabsf(x - g.x);
				return along <= FAN_HALF_LENGTH && across <= FAN_HALF_WIDTH;
				};
			auto push = [&](float& x, float& y) {
				x = std::clamp(x + g.dirX * FAN_PUSH_SPEED * dt, 0.0f, (float)MAP_WIDTH);
				y = std::clamp(y + g.dirY * FAN_PUSH_SPEED * dt, 0.0f, (float)MAP_HEIGHT);
				};
			for (ServerPlayer& p : players)
				if (p.active && p.hp > 0 && inWind(p.x, p.y))push(p.x, p.y);
			for (ServerEnemy& e : enemies)
				if (inWind(e.x, e.y))push(e.x, e.y);
		}

		if (remove) {
			gimmicks[i] = gimmicks.back();
			gimmicks.pop_back();
		}
		else ++i;
	}
}

void World::BroadcastGameResult(uint8_t result) {
	S2C_GameResult pkt{};
	InitPacket(pkt, S2C_GAME_RESULT);
	pkt.result = result;
	sendQ->Push(SendEvent{ TARGET_ALL, false, ToBytes(pkt) });
}

void World::CheckGameResult() {
	if (finished)return;
	if (bosses[BOSS_DASH_AND_BULLET - 1].killed) {// 보스3을 잡으면 클리어 (같은 틱에 전멸해도 클리어 우선)
		finished = true;
		BroadcastGameResult(RESULT_CLEAR);
		return;
	}
	for (const ServerPlayer& p : players)
		if (p.active && p.hp > 0)return;// 한 명이라도 살아 있으면 계속 (죽은 쪽은 관전)
	finished = true;
	BroadcastGameResult(RESULT_GAMEOVER);
}