#include <windows.h>
#include <windowsx.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

#define M_PI 3.14159265358979323846

bool debug = false;

enum GameState {
	GAME_START,
	GAME_MAP_SELECT,
	GAME_LOADING,
	GAME_PLAYING,
	GAME_PLAYING2
};

GameState currentState = GAME_START;
int selectedMap = -1;
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 800;
const int MAP_WIDTH = 2400;
const int MAP_HEIGHT = 2400;
const int TILE_SIZE = 32;

Bitmap* backgroundImg = nullptr;
// 메뉴 배경 이미지 추가
Bitmap* menuBackgroundImg = nullptr;

void DrawStartScreen(HDC hdc, HWND hwnd) {
	// === 메뉴 배경 이미지 출력 ===
	if (menuBackgroundImg) {
		Graphics graphics(hdc);
		graphics.DrawImage(menuBackgroundImg, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	}
}

void DrawMapSelectScreen(HDC hdc, HWND hwnd) {
	Image* mapPreview1 = new Image(L"Resources/Map/map_grid_preview.png");
	Image* mapPreview2 = new Image(L"Resources/Map/map_gimmick_preview.png");
	RECT rect;
	GetClientRect(hwnd, &rect);

	Graphics graphics(hdc);

	// 메뉴 배경
	if (menuBackgroundImg) {
		graphics.DrawImage(menuBackgroundImg, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	}

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(255, 255, 255));

	HFONT font = CreateFont(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
	HFONT oldFont = (HFONT)SelectObject(hdc, font);

	// 공통 설정
	const int buttonWidth = 600;
	const int buttonHeight = 150;
	const int imageSize = 125;
	const int textPadding = 20;
	const int textBoxHeight = 125;
	const int textBoxPadding = 10;

	int startX = 100;
	int startY = 150;

	HBRUSH btnBrush = CreateSolidBrush(RGB(40, 40, 40));
	HBRUSH textBrush = CreateSolidBrush(RGB(60, 60, 60));

	// === 버튼 1 ===
	RECT button1 = { startX, startY, startX + buttonWidth, startY + buttonHeight };
	FillRect(hdc, &button1, btnBrush);

	if (mapPreview1) {
		graphics.DrawImage(mapPreview1, button1.left + 10, button1.top + 10, imageSize, imageSize);
	}

	RECT textBox1 = {
		button1.left + imageSize + textPadding,
		button1.top + (buttonHeight - textBoxHeight) / 2,
		button1.right - 10,
		button1.top + (buttonHeight + textBoxHeight) / 2
	};
	FillRect(hdc, &textBox1, textBrush);
	DrawText(hdc, L"   격자 맵 - 보유기믹 : 없음", -1, &textBox1, DT_VCENTER | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

	// === 버튼 2 ===
	RECT button2 = { startX, startY + buttonHeight + 30, startX + buttonWidth, startY + 2 * buttonHeight + 30 };
	FillRect(hdc, &button2, btnBrush);

	if (mapPreview2) {
		graphics.DrawImage(mapPreview2, button2.left + 10, button2.top + 10, imageSize, imageSize);
	}

	RECT textBox2 = {
		button2.left + imageSize + textPadding,
		button2.top + (buttonHeight - textBoxHeight) / 2,
		button2.right - 10,
		button2.top + (buttonHeight + textBoxHeight) / 2
	};
	FillRect(hdc, &textBox2, textBrush);
	DrawText(hdc, L"   기믹 맵 - 보유기믹 : CD, Fan", -1, &textBox2, DT_VCENTER | DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

	// 정리
	DeleteObject(textBrush);
	DeleteObject(btnBrush);
	SelectObject(hdc, oldFont);
	DeleteObject(font);
}

int playerLevel = 1;
int killCount = 0;
DWORD gameStartTime = 0;
DWORD pauseTime = 0;

struct POINTF {
	float x;
	float y;
};

// === GIF 애니메이션 구조체 및 전역 ===
struct GifSprite {
	Image* image = nullptr;
	UINT frameCount = 0;
	UINT frameIndex = 0;
	std::vector<UINT> delays;
	DWORD lastFrameTime = 0;

	void Load(const wchar_t* filename) {
		image = new Image(filename);
		frameCount = image->GetFrameCount(&FrameDimensionTime);
		UINT size = image->GetPropertyItemSize(PropertyTagFrameDelay);
		if (size) {
			PropertyItem* pItem = (PropertyItem*)malloc(size);
			if (image->GetPropertyItem(PropertyTagFrameDelay, size, pItem) == Ok) {
				UINT* delays_ = (UINT*)pItem->value;
				for (UINT i = 0; i < frameCount; ++i)
					delays.push_back(delays_[i] * 10); // 1/100초 → ms
			}
			free(pItem);
		}
		lastFrameTime = GetTickCount();
	}

	void NextFrame() {
		if (!image || frameCount <= 1) return;
		DWORD now = GetTickCount();
		if (now - lastFrameTime >= delays[frameIndex]) {
			frameIndex = (frameIndex + 1) % frameCount;
			lastFrameTime = now;
		}
	}

	void Draw(Graphics& g, int x, int y, int w, int h, float angle = 0.0f) {
		if (!image) return;
		if (frameCount > 1) {
			GUID guid = FrameDimensionTime;
			image->SelectActiveFrame(&guid, frameIndex);
		}
		if (angle != 0.0f) {
			g.TranslateTransform((REAL)(x + w / 2), (REAL)(y + h / 2));
			g.RotateTransform(angle * 180.0f / (REAL)M_PI);
			g.TranslateTransform((REAL)-(x + w / 2), (REAL)-(y + h / 2));
		}
		g.DrawImage(image, x, y, w, h);
		if (angle != 0.0f) g.ResetTransform();
	}

	void Release() {
		if (image) { delete image; image = nullptr; }
		delays.clear();
	}
};

// === CD 기믹 ===
struct CDGimmick {
	float x, y;
	float dx, dy;  // 이동 방향
	bool active;
	float radius = 64.0f;
	int damage = 15;
	DWORD spawnTime;
};

// === 팬 기믹 ===
struct FanGimmick {
	float x, y;
	float startX, startY;
	float endX, endY;
	float windDx, windDy;  // 바람 방향
	float moveDx, moveDy;
	bool active;
	float width = 64.0f;   // 바람 폭
	float strength = 8.0f; // 밀림 강도
	float moveSpeed = 4.0f; // 바람 이동 속도 
	DWORD duration = 30000; // 30초간 지속
	DWORD spawnTime;
};

std::vector<CDGimmick> cdGimmicks;
std::vector<FanGimmick> fanGimmicks;

DWORD lastCDSpawnTime = 0;
DWORD lastFanSpawnTime = 0;
const DWORD CD_SPAWN_INTERVAL = 8000;  // 8초마다 CD 생성
const DWORD FAN_SPAWN_INTERVAL = 8000; // 12초마다 팬 생성

void SpawnCD() {
	CDGimmick cd;
	cd.active = true;
	cd.spawnTime = GetTickCount();

	// 랜덤하게 맵 가장자리에서 시작
	int side = rand() % 4;
	float speed = 6.0f;

	switch (side) {
	case 0: // 위에서 아래로
		cd.x = rand() % MAP_WIDTH;
		cd.y = 0;
		cd.dx = 0;
		cd.dy = speed;
		break;
	case 1: // 아래에서 위로
		cd.x = rand() % MAP_WIDTH;
		cd.y = MAP_HEIGHT;
		cd.dx = 0;
		cd.dy = -speed;
		break;
	case 2: // 왼쪽에서 오른쪽으로
		cd.x = 0;
		cd.y = rand() % MAP_HEIGHT;
		cd.dx = speed;
		cd.dy = 0;
		break;
	case 3: // 오른쪽에서 왼쪽으로
		cd.x = MAP_WIDTH;
		cd.y = rand() % MAP_HEIGHT;
		cd.dx = -speed;
		cd.dy = 0;
		break;
	}

	cdGimmicks.push_back(cd);
}

void SpawnFan() {
	FanGimmick fan;
	fan.active = true;
	fan.spawnTime = GetTickCount();

	// 랜덤하게 맵 한쪽 끝에서 다른 끝으로
	int direction = rand() % 4;

	switch (direction) {
	case 0: // 위에서 아래로
		fan.x = (float)(rand() % MAP_WIDTH);
		fan.y = 0.0f;
		fan.startX = 0;
		fan.startY = 0;
		fan.endX = MAP_WIDTH;
		fan.endY = 0;
		fan.windDx = 0;
		fan.windDy = fan.strength;
		fan.moveDx = 0.0f;
		fan.moveDy = fan.moveSpeed;  // 아래로 이동
		break;
	case 1: // 아래에서 위로
		fan.x = (float)(rand() % MAP_WIDTH);     // 맵 전체 폭에서 랜덤
		fan.y = (float)MAP_HEIGHT;
		fan.startX = 0;
		fan.startY = MAP_HEIGHT;
		fan.endX = MAP_WIDTH;
		fan.endY = MAP_HEIGHT;
		fan.windDx = 0;
		fan.windDy = -fan.strength;
		fan.moveDx = 0.0f;
		fan.moveDy = -fan.moveSpeed;  // 위로 이동
		break;
	case 2: // 왼쪽에서 오른쪽으로
		fan.x = 0.0f;
		fan.y = (float)(rand() % MAP_HEIGHT);    // 맵 전체 높이에서 랜덤
		fan.startX = 0.0f;
		fan.startY = fan.y;
		fan.endX = (float)MAP_WIDTH;
		fan.endY = fan.y;
		fan.windDx = fan.strength;
		fan.windDy = 0.0f;
		fan.moveDx = fan.moveSpeed;  // 오른쪽으로 이동
		fan.moveDy = 0.0f;
		break;
	case 3: // 오른쪽에서 왼쪽으로
		fan.x = (float)MAP_WIDTH;
		fan.y = (float)(rand() % MAP_HEIGHT);    // 맵 전체 높이에서 랜덤
		fan.startX = (float)MAP_WIDTH;
		fan.startY = fan.y;
		fan.endX = 0.0f;
		fan.endY = fan.y;
		fan.windDx = -fan.strength;
		fan.windDy = 0.0f;
		fan.moveDx = -fan.moveSpeed;  // 왼쪽으로 이동
		fan.moveDy = 0.0f;
		break;
	}

	fanGimmicks.push_back(fan);
}

// === 캐릭터 및 무기별 GIF 전역 인스턴스 ===
GifSprite playerGif, bossGif, boss2Gif, boss3Gif;
GifSprite enemyNormalGif, enemyFastGif, enemyTankGif;
GifSprite slashGif, circleGif, breathGif, slowGif, homingGif;
GifSprite cdGif, fanGif;
ULONG_PTR gdiToken;

//전역
#define playerhp 200
int playerHP = playerhp;    // HP 초기값 200
bool gameOver = false;

bool keyLeft = false;
bool keyRight = false;
bool keyUp = false;
bool keyDown = false;

POINT mousePos = { 0, 0 };
POINT playerPos = { MAP_WIDTH / 2, MAP_HEIGHT / 2 }; // 맵 중앙

enum WeaponType {
	WEAPON_SLOW,
	WEAPON_CIRCLE,
	WEAPON_BREATH,
	WEAPON_SLASH,
	WEAPON_COUNT  // 무기 총 개수
};

struct Weapon {
	WeaponType type;
	DWORD lastAttackTime;
	DWORD cooldown;
	DWORD duration;
	bool active;
	int damage;
};

struct HomingWeapon {
	bool active = false;
	float x, y;
	int targetIndex = -1;  // 타겟 적의 인덱스
	DWORD lastAttackTime = 0;
	DWORD duration = 3000; // 3초 동안 유지
	DWORD cooldown = 5000;
	float speed = 40.0f;
	int damage;
};

Weapon weapons[WEAPON_COUNT];

HomingWeapon homingWeapon;

//무기 관련 전역 변수
float weaponAngle = 0.0f; // 라디안
const int WEAPON_WIDTH = 128; // slash 무기 사이즈
const int WEAPON_HEIGHT = 32;
const float slashOffset = 64.0f;

const float CIRCLE_RADIUS = 75.0f;//원형 공격 사이즈
const float SLOW_RADIUS = 100.0f;//slow 사이즈
const float BREATH_RADIUS = 120.0f;//breath 사이즈

int cameraX = 0;
int cameraY = 0;

DWORD lastAttackTime = 0;          // 마지막 공격 시점
const DWORD attackCooldown = 1000;  // 쿨타임 0.5초
const DWORD weaponDisplayDuration = 200;  // 무기 유지 시간 0.2초

DWORD now = GetTickCount();
bool canDrawWeapon = (now - lastAttackTime >= attackCooldown);

bool isChoosingWeapon = false;
int weaponChoices[3] = { -1, -1, -1 };  // 무기 인덱스
int selectedWeapon = -1;               // 선택된 무기 인덱스 (0,1,2)

int weaponLevels[WEAPON_COUNT] = { 0 };  // 무기별 강화 레벨 저장

const char* GetWeaponName(int type) {
	switch (type) {
	case WEAPON_SLOW: return "홈매트";
	case WEAPON_CIRCLE: return "전기장";
	case WEAPON_BREATH: return "에프킬라";
	case WEAPON_SLASH: return "파리채";
	case WEAPON_COUNT: return "소금총";
	default: return "Unknown";
	}
}
Gdiplus::Image* weaponImages[5] = { nullptr };

void LoadWeaponImages() {
	weaponImages[WEAPON_SLASH] = new Image(L"Resources/Weapon/slash.png");
	weaponImages[WEAPON_CIRCLE] = new Image(L"Resources/Weapon/circle.png");
	weaponImages[WEAPON_BREATH] = new Image(L"Resources/Weapon/breath.png");
	weaponImages[WEAPON_SLOW] = new Image(L"Resources/Weapon/slow.png");
	weaponImages[WEAPON_COUNT] = new Image(L"Resources/Weapon/homing.png");
}

void DrawWeaponSelectionUI(HDC hdc, int screenWidth, int screenHeight) {
	if (!isChoosingWeapon) return;

	const int boxWidth = 200;
	const int boxHeight = 60;
	const int gap = 40;
	const int imgSize = 100;

	int totalWidth = 3 * boxWidth + 2 * gap;
	int startX = (screenWidth - totalWidth) / 2;
	int y = screenHeight * 3 / 5 - boxHeight / 2;

	HFONT hFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Segoe UI"));
	HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

	Graphics graphics(hdc);

	for (int i = 0; i < 3; ++i) {
		int x = startX + i * (boxWidth + gap);

		// 이미지 위치 계산
		int imgX = x + (boxWidth - imgSize) / 2;
		int imgY = y - imgSize - 20;

		// === 통합 외곽 박스 (이미지 + 텍스트 박스 포함) ===
		RECT outerBox = { x - 5, imgY - 20, x + boxWidth + 5, y + boxHeight + 5 };
		HBRUSH outerBrush = CreateSolidBrush(RGB(60, 60, 60));
		FillRect(hdc, &outerBox, outerBrush);
		DeleteObject(outerBrush);

		// 외곽 테두리 강조 (선택 시 노란색)
		HPEN outerPen = CreatePen(PS_SOLID, 4, (i == selectedWeapon) ? RGB(255, 255, 0) : RGB(100, 100, 100));
		HPEN oldPen = (HPEN)SelectObject(hdc, outerPen);
		SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
		Rectangle(hdc, outerBox.left, outerBox.top, outerBox.right, outerBox.bottom);
		SelectObject(hdc, oldPen);
		DeleteObject(outerPen);

		// === 이미지 출력 ===
		Image* weaponImg = weaponImages[weaponChoices[i]];
		if (weaponImg) {
			graphics.DrawImage(weaponImg, imgX, imgY, imgSize, imgSize);
		}

		// === 텍스트 박스 (선택 박스) ===
		RECT boxRect = { x, y, x + boxWidth, y + boxHeight };
		HBRUSH boxBrush = CreateSolidBrush(RGB(80, 80, 80));
		FillRect(hdc, &boxRect, boxBrush);
		DeleteObject(boxBrush);

		// 텍스트 출력
		const char* name = GetWeaponName(weaponChoices[i]);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(255, 255, 255));
		DrawTextA(hdc, name, -1, &boxRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	SelectObject(hdc, oldFont);
	DeleteObject(hFont);
}

void ReleaseWeaponImages() {
	for (int i = 0; i < WEAPON_COUNT; ++i) {
		delete weaponImages[i];
		weaponImages[i] = nullptr;
	}
}

void ShowWeaponSelection() {
	isChoosingWeapon = true;

	std::vector<int> indices = { 0, 1, 2, 3, 4 };

	// 난수 생성기 준비
	std::random_device rd;
	std::mt19937 g(rd());

	std::shuffle(indices.begin(), indices.end(), g);  // std::shuffle 사용!

	for (int i = 0; i < 3; ++i) {
		weaponChoices[i] = indices[i];
	}
}

void DrawWeaponUpgradeUI(HDC hdc) {
	if (!isChoosingWeapon) return;

	int width = WINDOW_WIDTH;
	int height = WINDOW_HEIGHT;

	int boxWidth = 300;
	int boxHeight = 200;
	int x = (width - boxWidth);
	int y = 0;

	Rectangle(hdc, x, y, x + boxWidth, y + boxHeight);

	// 무기 레벨 정보 표시
	TextOut(hdc, x + 10, y + 10, L"[무기 레벨 정보]", 11);
	for (int i = 0; i < WEAPON_COUNT; ++i) {
		wchar_t info[64];
		wsprintf(info, L"- %d: %d 레벨, %d damage", i, weaponLevels[i], weapons[i].damage);
		TextOut(hdc, x + 20, y + 30 + i * 20, info, lstrlen(info));
	}
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void DrawGrid(HDC hdc, int offsetX, int offsetY);

void CheckLevelUp(HWND hwnd) {
	if (playerLevel >= 15) return;

	// 레벨업 필요 처치 수 = 현재 레벨 * 5
	int requiredKills = playerLevel * 5;

	if (killCount >= requiredKills) {
		playerLevel++;
		killCount = 0;
		playerHP = playerhp;
		ShowWeaponSelection();  // 무기 선택 시작
	}
}

enum BossType {
	BOSS_NONE,
	BOSS_BASIC,
	BOSS_DASH_SHOOT, // 추가: 돌진 + 탄막형 보스
	BOSS_DASH_AND_BULLET,
};

struct Boss {
	float x, y;
	int hp, maxHp;
	bool active;
	BossType type;

	Boss() : x(0), y(0), hp(0), maxHp(0), active(false), type(BOSS_NONE) {}

	void Init(BossType t, float px, float py) {
		type = t;
		active = true;
		x = px;
		y = py;

		switch (t) {
		case BOSS_BASIC:
			hp = maxHp = 1000;
			break;
		case BOSS_DASH_SHOOT:
			hp = maxHp = 800; // 예시: 덜 단단한 대신 공격적
			break;
		case BOSS_DASH_AND_BULLET:
			hp = maxHp = 1200;
			break;
		}
	}

	void Draw(HDC hdc, int camX, int camY) {
		if (!active) return;

		int screenX = x - camX;
		int screenY = y - camY;
		RECT rect = { screenX - 64, screenY - 64, screenX + 64, screenY + 64 };

		HBRUSH brush = CreateSolidBrush(RGB(150, 0, 200));
		FillRect(hdc, &rect, brush);
		DeleteObject(brush);

		HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
		HPEN oldPen = (HPEN)SelectObject(hdc, pen);
		SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
		Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
		SelectObject(hdc, oldPen);
		DeleteObject(pen);
	}

	void DrawHealthBar(HDC hdc, int screenWidth) {
		if (!active) return;

		int barWidth = screenWidth - 40;
		int barHeight = 20;
		int x = 20;
		int y = 20;

		// 테두리
		Rectangle(hdc, x, y, x + barWidth, y + barHeight);

		// 체력 비율
		float ratio = (float)hp / maxHp;
		int filledWidth = (int)(barWidth * ratio);

		HBRUSH red = CreateSolidBrush(RGB(200, 0, 0));
		RECT healthRect = { x, y, x + filledWidth, y + barHeight };
		FillRect(hdc, &healthRect, red);
		DeleteObject(red);
	}
};

struct BossBullet {
	float x, y;      // 위치
	float dx, dy;    // 방향 (단위 벡터 * 속도)
	float speed;
	float radius = 8.0f; // 충돌 반경
};

bool showBossAlert = false;
DWORD bossAlertStartTime = 0;
const DWORD bossAlertDuration = 3000;  // 3초

void DrawBossAlert(HDC hdc) {
	DWORD now = GetTickCount();

	if (showBossAlert && now - bossAlertStartTime <= bossAlertDuration) {
		RECT alertRect = { 0, 200, 800, 400 };  // 사각형 위치
		HBRUSH redBrush = CreateSolidBrush(RGB(255, 0, 0));
		FillRect(hdc, &alertRect, redBrush);
		DeleteObject(redBrush);

		// 텍스트 추가 (선택사항)
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(255, 255, 255));
		DrawText(hdc, L"WARNING!", -1, &alertRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else {
		showBossAlert = false;  // 3초가 지나면 숨김
	}
}

Boss boss;
DWORD lastTargetShotTime = 0;
const DWORD shootInterval = 2000;
std::vector<BossBullet> bossBullets;
DWORD lastBossAttackTime = 0;
const DWORD bossAttackInterval = 3000; // 3초마다 공격

const DWORD dashInterval = 10000;

Boss boss2;
bool isDashing = false;
DWORD lastDashTime = 0;
float dashDx = 0, dashDy = 0;
DWORD dashStartTime = 0;
const DWORD dashDuration = 1000; // 돌진 유지 시간 (ms)
const float dashSpeed = 8.0f;

Boss boss3;
bool isDashing3 = false;
DWORD lastboss3AttackTime = 0;
DWORD lastboss3DashTime = 0;
DWORD lastboss3DashStartTime = 0;
const DWORD boss3AttackInterval = 2500;
const DWORD boss3DashInterval = 4000;

DWORD bossAttackLastTime = 0;
DWORD bossLastAttackTick = 0;
int bossAttackStep = 0;
const DWORD bossAttackCooldown = 5000;  // 5초마다 공격 시작
const DWORD intervalBetweenAttacks = 300;  // 연속 공격 간격
const int bossBaseRange = 150;  // 첫 번째 공격 범위
const int bossRangeStep = 50;   // 공격할 때마다 증가할 범위
const int bossAttackCount = 3;  // 총 3회 연속 공격
const int bossAttackDamage = 20;

bool killedBoss = false;
bool killedBoss2 = false;
bool killedBoss3 = false;

void SpawnBoss() {
	if (boss.active) return;

	float spawnX = playerPos.x + 200;
	float spawnY = playerPos.y - 100;
	boss.Init(BOSS_BASIC, spawnX, spawnY);
}
void Spawnboss2() {
	if (boss2.active) return;

	float spawnX = playerPos.x + 200;
	float spawnY = playerPos.y - 100;
	boss2.Init(BOSS_DASH_SHOOT, spawnX, spawnY);
}

void Spawnboss3() {
	if (boss3.active) return;

	float spawnX = playerPos.x + 200;
	float spawnY = playerPos.y - 100;
	boss3.Init(BOSS_DASH_AND_BULLET, spawnX, spawnY);
}

enum EnemyType {
	ENEMY_NORMAL,
	ENEMY_FAST,
	ENEMY_TANK
};

struct Enemy {
	float x, y;
	int hp;
	int type = ENEMY_NORMAL;
	int slowUntil = 0;

	float GetSpeed() const {
		float base = (type == ENEMY_FAST) ? 2.0f : 1.0f;
		if (GetTickCount() < slowUntil)
			return base * 0.5f;  // 슬로우 상태일 경우
		return base;
	}
};

std::vector<Enemy> enemies;
DWORD lastSpawnTime = 0;

//화면 외곽에서 적 생성
void SpawnEnemy(int offsetX, int offsetY) {
	RECT spawnZone = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	int edge = rand() % 4;
	Enemy e;

	switch (edge) {
	case 0: // 위
		e.x = cameraX + rand() % WINDOW_WIDTH;
		e.y = cameraY - 32;
		break;
	case 1: // 아래
		e.x = cameraX + rand() % WINDOW_WIDTH;
		e.y = cameraY + WINDOW_HEIGHT + 32;
		break;
	case 2: // 왼쪽
		e.x = cameraX - 32;
		e.y = cameraY + rand() % WINDOW_HEIGHT;
		break;
	case 3: // 오른쪽
		e.x = cameraX + WINDOW_WIDTH + 32;
		e.y = cameraY + rand() % WINDOW_HEIGHT;
		break;
	}

	// 화면 기준 좌표 → 맵 좌표 변환
	// 30% 확률로 빠른 적 생성
	if (rand() % 100 < 20) {
		e.type = ENEMY_FAST;
		e.hp = 10;  // 예: 빠른 적은 체력 10
	}
	else if (rand() % 100 < 40) {
		e.type = ENEMY_TANK;
		e.hp = 20;  // 예: 빠른 적은 체력 10
	}
	else {
		e.type = ENEMY_NORMAL;
		e.hp = 10;  // 예: 일반 적은 체력 20
	}

	enemies.push_back(e);
}

int GetRequiredKills(int level) {
	return level * 5; // 예: 1레벨 -> 5마리, 2레벨 -> 10마리 ...
}

int GetPrevRequiredKills(int level) {
	return (level - 1) * 5;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"MyGameWindow";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClass(&wc);

	RECT rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindow(L"MyGameWindow", L"BUG PATCHER",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, hInstance, NULL);

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

//맵 격자 나중에 지워도 됨
void DrawGrid(HDC hdc, int offsetX, int offsetY) {
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
	HPEN oldPen = (HPEN)SelectObject(hdc, pen);

	int startX = offsetX / TILE_SIZE;
	int startY = offsetY / TILE_SIZE;

	int endX = (offsetX + WINDOW_WIDTH) / TILE_SIZE + 1;
	int endY = (offsetY + WINDOW_HEIGHT) / TILE_SIZE + 1;

	for (int y = startY; y <= endY; ++y) {
		for (int x = startX; x <= endX; ++x) {
			int screenX = x * TILE_SIZE - offsetX;
			int screenY = y * TILE_SIZE - offsetY;
			Rectangle(hdc, screenX, screenY, screenX + TILE_SIZE, screenY + TILE_SIZE);
		}
	}

	SelectObject(hdc, oldPen);
	DeleteObject(pen);
}

void DrawExpBar(HDC hdc) {
	const int barX = 10;
	const int barY = 10;
	const int barWidth = 300;
	const int barHeight = 20;

	// 배경
	HBRUSH bgBrush = CreateSolidBrush(RGB(50, 50, 50));
	RECT bgRect = { barX, barY, barX + barWidth, barY + barHeight };
	FillRect(hdc, &bgRect, bgBrush);
	DeleteObject(bgBrush);

	// 현재 레벨에서 필요한 킬 수
	int requiredKills = playerLevel * 5;
	if (requiredKills == 0) requiredKills = 1;

	int progressWidth = barWidth * killCount / requiredKills;

	// 진행 바
	HBRUSH fillBrush = CreateSolidBrush(RGB(0, 200, 255)); // 밝은 파란색
	RECT fillRect = { barX, barY, barX + progressWidth, barY + barHeight };
	FillRect(hdc, &fillRect, fillBrush);
	DeleteObject(fillBrush);

	// 텍스트 표시
	TCHAR text[64];
	wsprintf(text, TEXT("Level %d (%d/%d)"), playerLevel, killCount, requiredKills);
	SetBkMode(hdc, TRANSPARENT);
	//SetTextColor(hdc, RGB(255, 255, 255));
	TextOut(hdc, barX + 5, barY + 2, text, lstrlen(text));
}

void DrawHealthBar(HDC hdc) {
	const int maxHP = 200;
	const int barWidth = 64;
	const int barHeight = 8;

	// 플레이어 화면 위치
	int screenPlayerX = playerPos.x - cameraX;
	int screenPlayerY = playerPos.y - cameraY;

	int barX = screenPlayerX - barWidth / 2;
	int barY = screenPlayerY + 32 + 4; // 플레이어 하단 + 여백

	// 배경 바
	HBRUSH bgBrush = CreateSolidBrush(RGB(50, 50, 50));
	RECT bgRect = { barX, barY, barX + barWidth, barY + barHeight };
	FillRect(hdc, &bgRect, bgBrush);
	DeleteObject(bgBrush);

	// 체력 진행 바
	int fillWidth = barWidth * playerHP / maxHP;
	HBRUSH hpBrush = CreateSolidBrush(RGB(255, 80, 80)); // 빨간색
	RECT fillRect = { barX, barY, barX + fillWidth, barY + barHeight };
	FillRect(hdc, &fillRect, hpBrush);
	DeleteObject(hpBrush);

	// 테두리
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
	HPEN oldPen = (HPEN)SelectObject(hdc, pen);
	SelectObject(hdc, GetStockObject(NULL_BRUSH));
	Rectangle(hdc, barX, barY, barX + barWidth, barY + barHeight);
	SelectObject(hdc, oldPen);
	DeleteObject(pen);
}

bool CheckCollision(const Enemy& e) {
	float dx = playerPos.x - e.x;
	float dy = playerPos.y - e.y;
	float distSq = dx * dx + dy * dy;
	float minDist = 32.0f; // 플레이어와 적이 32px 이하 거리면 충돌
	return distSq < (minDist * minDist);
}

void UpdateCamera() {
	cameraX = playerPos.x - WINDOW_WIDTH / 2;
	cameraY = playerPos.y - WINDOW_HEIGHT / 2;

	// 맵 경계 안으로 제한
	if (cameraX < 0) cameraX = 0;
	if (cameraY < 0) cameraY = 0;
	if (cameraX > MAP_WIDTH - WINDOW_WIDTH) cameraX = MAP_WIDTH - WINDOW_WIDTH;
	if (cameraY > MAP_HEIGHT - WINDOW_HEIGHT) cameraY = MAP_HEIGHT - WINDOW_HEIGHT;
}

bool PointInPolygon(const POINT* poly, int n, float x, float y) {
	int count = 0;
	for (int i = 0; i < n; ++i) {
		POINT p1 = poly[i];
		POINT p2 = poly[(i + 1) % n];

		if ((p1.y > y) != (p2.y > y)) {
			float atX = (float)(p2.x - p1.x) * (y - p1.y) / (p2.y - p1.y) + p1.x;
			if (x < atX)
				count++;
		}
	}
	return (count % 2) == 1;
}

bool RectPolygonCollision(const POINT* poly, int polyCount, float rectX, float rectY, float rectW, float rectH) {
	// 1. 적 사각형의 4점이 무기 폴리곤 안에 있는지
	POINT rectPts[4] = {
		{(int)rectX, (int)rectY},
		{(int)(rectX + rectW), (int)rectY},
		{(int)(rectX + rectW), (int)(rectY + rectH)},
		{(int)rectX, (int)(rectY + rectH)},
	};

	for (int i = 0; i < 4; i++) {
		if (PointInPolygon(poly, polyCount, (float)rectPts[i].x, (float)rectPts[i].y)) {
			return true;
		}
	}

	// 2. 무기 폴리곤의 점이 적 사각형 내부에 있는지
	for (int i = 0; i < polyCount; i++) {
		float px = (float)poly[i].x;
		float py = (float)poly[i].y;
		if (px >= rectX && px <= rectX + rectW &&
			py >= rectY && py <= rectY + rectH) {
			return true;
		}
	}

	return false;
}

void weaponCoolTime(Weapon& w) {
	//공격 판정
	DWORD now = GetTickCount();

	// 무기 상태 갱신 및 충돌 처리
	if (!w.active && now - w.lastAttackTime >= w.cooldown) {
		w.active = true;
		w.lastAttackTime = now;
	}

	if (w.active && now - w.lastAttackTime >= w.duration) {
		w.active = false;
	}
}

int FindBestTarget() {
	int bestIndex = -1;
	int maxHp = -1;
	float minDist = FLT_MAX;

	for (int i = 0; i < enemies.size(); ++i) {
		const Enemy& e = enemies[i];

		// 화면 내에 있는 적만 고려
		if (e.x < cameraX || e.x > cameraX + WINDOW_WIDTH ||
			e.y < cameraY || e.y > cameraY + WINDOW_HEIGHT) continue;

		if (e.hp > maxHp) {
			maxHp = e.hp;
			bestIndex = i;
			minDist = sqrtf((playerPos.x - e.x) * (playerPos.x - e.x) +
				(playerPos.y - e.y) * (playerPos.y - e.y));
		}
		else if (e.hp == maxHp) {
			float dist = sqrtf((playerPos.x - e.x) * (playerPos.x - e.x) +
				(playerPos.y - e.y) * (playerPos.y - e.y));
			if (dist < minDist) {
				bestIndex = i;
				minDist = dist;
			}
		}
	}
	return bestIndex;
}

void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
	if (isChoosingWeapon) return;

	switch (idEvent) {
	case 1: { // 게임 루프 (프레임마다 실행)
		if (showBossAlert||gameOver||killedBoss3) return;
		const int speed = 4;

		if (keyLeft)  playerPos.x -= speed;
		if (keyRight) playerPos.x += speed;
		if (keyUp)    playerPos.y -= speed;
		if (keyDown)  playerPos.y += speed;

		// 맵 경계 체크
		if (playerPos.x < 0) playerPos.x = 0;
		if (playerPos.y < 0) playerPos.y = 0;
		if (playerPos.x > MAP_WIDTH) playerPos.x = MAP_WIDTH;
		if (playerPos.y > MAP_HEIGHT) playerPos.y = MAP_HEIGHT;

		UpdateCamera();

		if (currentState == GAME_PLAYING2) {
			// === CD 기믹 생성 ===
			DWORD now = GetTickCount();
			if (now - lastCDSpawnTime >= CD_SPAWN_INTERVAL) {
				SpawnCD();
				lastCDSpawnTime = now;
			}

			// === 팬 기믹 생성 ===
			if (now - lastFanSpawnTime >= FAN_SPAWN_INTERVAL) {
				SpawnFan();
				lastFanSpawnTime = now;
			}
		}

		// === CD 이동 및 충돌 처리 ===
		for (auto it = cdGimmicks.begin(); it != cdGimmicks.end(); ) {
			it->x += it->dx;
			it->y += it->dy;

			// 플레이어와 충돌 체크
			float dx = it->x - playerPos.x;
			float dy = it->y - playerPos.y;
			float distSq = dx * dx + dy * dy;

			if (distSq <= (it->radius * it->radius)) {
				playerHP -= it->damage;
				it = cdGimmicks.erase(it);
				continue;
			}

			// 맵 밖으로 나가면 제거
			if (it->x < -50 || it->x > MAP_WIDTH + 50 ||
				it->y < -50 || it->y > MAP_HEIGHT + 50) {
				it = cdGimmicks.erase(it);
				continue;
			}

			++it;
		}

		// === 팬 기믹 처리 ===
		for (auto it = fanGimmicks.begin(); it != fanGimmicks.end(); ) {
			DWORD elapsed = now - it->spawnTime;

			if (elapsed >= it->duration) {
				it = fanGimmicks.erase(it);
				continue;
			}

			// === 바람 이동 처리 ===
			it->x += it->moveDx;
			it->y += it->moveDy;

			// 맵 밖으로 나가면 제거
			if (it->x < -200 || it->x > MAP_WIDTH + 200 ||
				it->y < -200 || it->y > MAP_HEIGHT + 200) {
				it = fanGimmicks.erase(it);
				continue;
			}

			// 플레이어 밀림 처리 (현재 바람 위치 기준으로 수정)
			bool playerInWind = false;
			if (it->windDx != 0) { // 수평 바람
				if (abs((int)playerPos.y - (int)it->y) <= (int)it->width &&
					abs((int)playerPos.x - (int)it->x) <= 100) {
					playerInWind = true;
				}
			}
			else { // 수직 바람
				if (abs((int)playerPos.x - (int)it->x) <= (int)it->width &&
					abs((int)playerPos.y - (int)it->y) <= 100) {
					playerInWind = true;
				}
			}

			if (playerInWind) {
				playerPos.x += it->windDx;
				playerPos.y += it->windDy;

				// 맵 경계 체크
				if (playerPos.x < 0) playerPos.x = 0;
				if (playerPos.y < 0) playerPos.y = 0;
				if (playerPos.x > MAP_WIDTH) playerPos.x = MAP_WIDTH;
				if (playerPos.y > MAP_HEIGHT) playerPos.y = MAP_HEIGHT;
			}

			// 적들 밀림 처리 (현재 바람 위치 기준으로 수정)
			for (auto& enemy : enemies) {
				bool enemyInWind = false;
				if (it->windDx != 0) { // 수평 바람
					if (abs((int)enemy.y - (int)it->y) <= (int)it->width &&
						abs((int)enemy.x - (int)it->x) <= 100) {
						enemyInWind = true;
					}
				}
				else { // 수직 바람
					if (abs((int)enemy.x - (int)it->x) <= (int)it->width &&
						abs((int)enemy.y - (int)it->y) <= 100) {
						enemyInWind = true;
					}
				}

				if (enemyInWind) {
					enemy.x += it->windDx;
					enemy.y += it->windDy;

					// 적 맵 경계 체크
					if (enemy.x < 0) enemy.x = 0;
					if (enemy.y < 0) enemy.y = 0;
					if (enemy.x > MAP_WIDTH) enemy.x = MAP_WIDTH;
					if (enemy.y > MAP_HEIGHT) enemy.y = MAP_HEIGHT;
				}
			}

			++it;
		}

		// 적 생성 (1초마다)
		if (playerLevel < 15) {
			if (GetTickCount() - lastSpawnTime > 1000) {
				int offsetX = playerPos.x - WINDOW_WIDTH / 2;
				int offsetY = playerPos.y - WINDOW_HEIGHT / 2;

				offsetX = max(0, min(offsetX, MAP_WIDTH - WINDOW_WIDTH));
				offsetY = max(0, min(offsetY, MAP_HEIGHT - WINDOW_HEIGHT));

				// 경과 시간 (초)
				DWORD elapsed = (GetTickCount() - gameStartTime) / 1000;

				// 10초에 1마리씩 추가되게
				int spawnCount = 1 + elapsed / 10;
				spawnCount = min(spawnCount, 10); // 최대 10마리

				for (int i = 0; i < spawnCount; ++i) {
					SpawnEnemy(offsetX, offsetY);
				}

				lastSpawnTime = GetTickCount();
			}
		}

		// 적 이동
		for (auto& enemy : enemies) {
			float dx = playerPos.x - enemy.x;
			float dy = playerPos.y - enemy.y;
			float len = sqrt(dx * dx + dy * dy);

			if (len > 0.0f) {
				float speed = enemy.GetSpeed();  // 타입에 따라 속도 결정
				enemy.x += dx / len * speed;
				enemy.y += dy / len * speed;
			}
		}

		//나방
		//보스 스폰
		DWORD elapsed = GetTickCount() - gameStartTime;
		if (elapsed > 120000 && !boss.active && !killedBoss) {
			SpawnBoss();
			showBossAlert = true;
			bossAlertStartTime = GetTickCount();
		}
		if (elapsed > 180000 && !boss2.active && !killedBoss2) {
			Spawnboss2();
			showBossAlert = true;
			bossAlertStartTime = GetTickCount();
		}
		if (elapsed > 240000 && !boss3.active && !killedBoss3) {
			Spawnboss3();
			showBossAlert = true;
			bossAlertStartTime = GetTickCount();
		}

		//보스 이동
		if (boss.active) {
			float dx = playerPos.x - boss.x;
			float dy = playerPos.y - boss.y;
			float len = sqrtf(dx * dx + dy * dy);

			// 기본 이동
			if (len > 0.1f) {
				float bossSpeed = 1.5f;
				boss.x += dx / len * bossSpeed;
				boss.y += dy / len * bossSpeed;
			}

			if (boss.type == BOSS_BASIC) {
				// 기존 8방향 탄막
				if (GetTickCount() - lastBossAttackTime >= bossAttackInterval) {
					lastBossAttackTime = GetTickCount();
					const int numBullets = 8;
					for (int i = 0; i < numBullets; ++i) {
						float angle = i * (2 * M_PI / numBullets);
						BossBullet b = { boss.x, boss.y, cosf(angle) * 5.0f, sinf(angle) * 5.0f, 5.0f };
						bossBullets.push_back(b);
					}
				}
			}

		}
		if (boss2.active) {
			float dx = playerPos.x - boss2.x;
			float dy = playerPos.y - boss2.y;
			float len = sqrtf(dx * dx + dy * dy);

			// 기본 이동
			if (len > 0.1f) {
				float bossSpeed = 1.5f;
				boss2.x += dx / len * bossSpeed;
				boss2.y += dy / len * bossSpeed;
			}
			// 1) 주기적인 플레이어 방향 탄막
			if (GetTickCount() - lastTargetShotTime >= shootInterval) {
				lastTargetShotTime = GetTickCount();

				float tx = playerPos.x - boss2.x;
				float ty = playerPos.y - boss2.y;
				float len = sqrtf(tx * tx + ty * ty);
				if (len > 0.0f) {
					float vx = tx / len * 30.0f;
					float vy = ty / len * 30.0f;
					BossBullet b = { boss2.x, boss2.y, vx, vy, 6.0f };
					bossBullets.push_back(b);
				}
			}

			// 2) 주기적인 돌진
			if (!isDashing && GetTickCount() - lastDashTime >= dashInterval) {
				lastDashTime = GetTickCount();
				dashStartTime = GetTickCount();
				isDashing = true;

				float tx = playerPos.x - boss2.x;
				float ty = playerPos.y - boss2.y;
				float len = sqrtf(tx * tx + ty * ty);
				if (len > 0.0f) {
					dashDx = tx / len;
					dashDy = ty / len;
				}
			}
			if (isDashing) {
				boss2.x += dashDx * dashSpeed;
				boss2.y += dashDy * dashSpeed;

				if (GetTickCount() - dashStartTime >= dashDuration) {
					isDashing = false; // 돌진 종료
				}
			}
		}
		// 보스3 이동
		if (boss3.active) {
			float dx = playerPos.x - boss3.x;
			float dy = playerPos.y - boss3.y;
			float len = sqrtf(dx * dx + dy * dy);

			if (len > 0.1f) {
				float speed = 1.2f;
				boss3.x += dx / len * speed;
				boss3.y += dy / len * speed;
			}

			DWORD now = GetTickCount();

			// 8방향 탄막
			if (now - lastboss3AttackTime >= boss3AttackInterval) {
				lastboss3AttackTime = now;
				for (int i = 0; i < 8; ++i) {
					float angle = i * (2 * M_PI / 8);
					BossBullet b;
					b.x = boss3.x;
					b.y = boss3.y;
					b.speed = 5.0f;
					b.dx = cosf(angle) * b.speed;
					b.dy = sinf(angle) * b.speed;
					bossBullets.push_back(b);
				}
			}

			// 2) 주기적인 돌진
			if (!isDashing3 && GetTickCount() - lastboss3DashTime >= dashInterval) {
				lastboss3DashTime = GetTickCount();
				lastboss3DashStartTime = GetTickCount();
				isDashing3 = true;

				float tx = playerPos.x - boss3.x;
				float ty = playerPos.y - boss3.y;
				float len = sqrtf(tx * tx + ty * ty);
				if (len > 0.0f) {
					dashDx = tx / len;
					dashDy = ty / len;
				}
			}
			if (isDashing3) {
				boss3.x += dashDx * dashSpeed;
				boss3.y += dashDy * dashSpeed;

				if (GetTickCount() - lastboss3DashStartTime >= dashDuration) {
					isDashing3 = false; // 돌진 종료
				}
			}

			//광역 공격
			// 1. 쿨타임 지난 경우 공격 시작
			if (now - bossAttackLastTime >= bossAttackCooldown && bossAttackStep == 0) {
				bossAttackStep = 1;
				bossLastAttackTick = now;
			}

			// 2. 공격 중이라면
			if (bossAttackStep > 0 && bossAttackStep <= bossAttackCount) {
				if (now - bossLastAttackTick >= intervalBetweenAttacks) {
					bossLastAttackTick = now;

					// 공격 범위 계산
					int currentRange = bossBaseRange + (bossAttackStep - 1) * bossRangeStep;

					float dx = playerPos.x - boss3.x;
					float dy = playerPos.y - boss3.y;
					float distance = sqrtf(dx * dx + dy * dy);

					if (distance <= currentRange) {
						playerHP -= bossAttackDamage;
						// 필요 시 피격 효과 추가
					}

					bossAttackStep++;

					// 공격 완료
					if (bossAttackStep > bossAttackCount) {
						bossAttackStep = 0;
						bossAttackLastTime = now;
					}
				}
			}

		}
		//보스 탄막 충돌체크
		for (auto it = bossBullets.begin(); it != bossBullets.end(); ) {
			it->x += it->dx;
			it->y += it->dy;

			// 플레이어와 충돌
			float dx = it->x - playerPos.x;
			float dy = it->y - playerPos.y;
			float distSq = dx * dx + dy * dy;
			float totalRadius = 16.0f + it->radius; // 플레이어 크기 고려

			if (distSq <= totalRadius * totalRadius) {
				playerHP -= 10; // 데미지
				it = bossBullets.erase(it);
				continue;
			}
			// 화면 밖 제거
			if (it->x < 0 || it->x > MAP_WIDTH || it->y < 0 || it->y > MAP_HEIGHT) {
				it = bossBullets.erase(it);
				continue;
			}

			++it;
		}
		
		if (boss.active && boss.hp <= 0) {
			boss.active = false;
			// 보스 처치 시 효과 (선택)
			killedBoss = true;	
			playerHP = playerhp;
		}
		if (boss2.active && boss2.hp <= 0) {
			boss2.active = false;
			// 보스 처치 시 효과 (선택)
			killedBoss2 = true;
			playerHP = playerhp;
		}
		if (boss3.active && boss3.hp <= 0) {
			boss3.active = false;
			// 보스 처치 시 효과 (선택)
			killedBoss3 = true;
			playerHP = playerhp;
		}
		CheckLevelUp(hwnd);
		InvalidateRect(hwnd, NULL, FALSE);
		break;
	}
	case 2: { // 충돌 체크 (0.1초마다)
		if (debug) return;
		int collisionCount = 0;

		for (const auto& e : enemies) {
			if (CheckCollision(e)) {
				collisionCount++;
			}
		}

		if (collisionCount > 0) {
			playerHP -= collisionCount;
		}
		if (playerHP <= 0) {
			gameOver = true;
		}
		if (boss.active) {
			float dx = boss.x - playerPos.x;
			float dy = boss.y - playerPos.y;
			float distSq = dx * dx + dy * dy;

			float bossRadius = 64.0f;
			float playerRadius = 16.0f; // 주인공 반지름 (대충 추정)
			float totalRadius = bossRadius + playerRadius;

			if (distSq <= totalRadius * totalRadius) {
				// 충돌 발생 시 데미지
				playerHP -= 5;
			}
		}
		if (boss2.active) {
			float dx = boss2.x - playerPos.x;
			float dy = boss2.y - playerPos.y;
			float distSq = dx * dx + dy * dy;

			float bossRadius = 64.0f;
			float playerRadius = 16.0f; // 주인공 반지름 (대충 추정)
			float totalRadius = bossRadius + playerRadius;

			if (distSq <= totalRadius * totalRadius) {
				// 충돌 발생 시 데미지
				playerHP -= 20;
			}
		}
		if (boss3.active) {
			float dx = boss3.x - playerPos.x;
			float dy = boss3.y - playerPos.y;
			float distSq = dx * dx + dy * dy;

			float bossRadius = 64.0f;
			float playerRadius = 16.0f; // 주인공 반지름 (대충 추정)
			float totalRadius = bossRadius + playerRadius;

			if (distSq <= totalRadius * totalRadius) {
				// 충돌 발생 시 데미지
				playerHP -= 10;
			}
		}
		break;
	}
	case 11: {//slash
		// 마우스 위치 기준 회전 각도 계산
		int screenPlayerX = playerPos.x - cameraX;
		int screenPlayerY = playerPos.y - cameraY;

		float dx = (float)(mousePos.x - screenPlayerX);
		float dy = (float)(mousePos.y - screenPlayerY);
		weaponAngle = atan2f(dy, dx); // 마우스 방향 각도 (라디안)

		// 무기 위치 및 폴리곤 계산
		int centerX = playerPos.x - cameraX;
		int centerY = playerPos.y - cameraY;

		float weaponCenterX = playerPos.x + cosf(weaponAngle) * slashOffset;
		float weaponCenterY = playerPos.y + sinf(weaponAngle) * slashOffset;

		POINT weaponPoly[4];
		for (int i = 0; i < 4; i++) {
			float x = (i < 2 ? -WEAPON_WIDTH / 2.0f : WEAPON_WIDTH / 2.0f);
			float y = (i % 3 == 0 ? -WEAPON_HEIGHT / 2.0f : WEAPON_HEIGHT / 2.0f);

			float rotatedX = x * cosf(weaponAngle) - y * sinf(weaponAngle);
			float rotatedY = x * sinf(weaponAngle) + y * cosf(weaponAngle);

			weaponPoly[i].x = (int)(weaponCenterX + rotatedX);
			weaponPoly[i].y = (int)(weaponCenterY + rotatedY);
		}

		weaponCoolTime(weapons[WEAPON_SLASH]);

		// 무기가 활성화된 경우 충돌 체크
		if (weapons[WEAPON_SLASH].active) {
			// 회전 사각형 충돌 처리
			// weaponPoly는 전역 혹은 적절한 위치에 정의되어 있어야 합니다.
			for (auto it = enemies.begin(); it != enemies.end(); ) {
				float ex = it->x - 16;
				float ey = it->y - 16;

				if (RectPolygonCollision(weaponPoly, 4, ex, ey, slashOffset, slashOffset)) {
					it->hp -= weapons[WEAPON_SLASH].damage;
					if (it->hp <= 0) {
						it = enemies.erase(it);
						killCount++;
						continue;
					}

				}
				++it;
			}
			// 보스 충돌 처리 추가
			if (boss.active && boss.hp > 0) {
				if (RectPolygonCollision(weaponPoly, 4, boss.x - 64, boss.y - 64, 128.0f, 128.0f)) {
					boss.hp -= weapons[WEAPON_SLASH].damage;
				}
			}
			if (boss2.active && boss2.hp > 0) {
				if (RectPolygonCollision(weaponPoly, 4, boss2.x - 64, boss2.y - 64, 128.0f, 128.0f)) {
					boss2.hp -= weapons[WEAPON_SLASH].damage;
				}
			}
			if (boss3.active && boss3.hp > 0) {
				if (RectPolygonCollision(weaponPoly, 4, boss3.x - 64, boss3.y - 64, 128.0f, 128.0f)) {
					boss3.hp -= weapons[WEAPON_SLASH].damage;
				}
			}
		}
		InvalidateRect(hwnd, NULL, FALSE);
		break;
	}
	case 12: {//circle
		// 원형 범위 충돌 처리
		for (auto it = enemies.begin(); it != enemies.end(); ) {
			float dx = it->x - playerPos.x;
			float dy = it->y - playerPos.y;
			float distSq = dx * dx + dy * dy;

			if (distSq <= CIRCLE_RADIUS * CIRCLE_RADIUS) {
				it->hp -= weapons[WEAPON_CIRCLE].damage;
				if (it->hp <= 0) {
					it = enemies.erase(it);
					killCount++;
					continue;
				}
			}
			++it;
		}
		if (boss.active) {
			float dx = boss.x - playerPos.x;
			float dy = boss.y - playerPos.y;
			float distSq = dx * dx + dy * dy;

			if (distSq <= CIRCLE_RADIUS * CIRCLE_RADIUS) {
				boss.hp -= weapons[WEAPON_CIRCLE].damage;  // 또는 데미지 수치 조정
			}
		}
		break;
	}
	case 13: { // BREATH 무기 자동 발동
		weaponCoolTime(weapons[WEAPON_BREATH]);

		if (!weapons[WEAPON_BREATH].active) break;

		// 공격 판정
		float cx = playerPos.x;
		float cy = playerPos.y;

		// 일반 적 대상
		for (auto it = enemies.begin(); it != enemies.end(); ) {
			float dx = it->x - cx;
			float dy = it->y - cy;
			float distSq = dx * dx + dy * dy;

			if (distSq <= BREATH_RADIUS * BREATH_RADIUS) {
				float angleToEnemy = atan2f(dy, dx);
				float angleDiff = fabsf(weaponAngle - angleToEnemy);

				if (angleDiff > M_PI) angleDiff = 2 * M_PI - angleDiff;

				if (angleDiff <= M_PI / 6) { // 60도 범위 내
					it->hp -= weapons[WEAPON_BREATH].damage;

					if (it->hp <= 0) {
						it = enemies.erase(it);
						killCount++;

						continue;
					}
				}
			}
			++it;
		}

		// 보스 대상
		if (boss.active && boss.hp > 0) {
			float dx = boss.x - cx;
			float dy = boss.y - cy;
			float distSq = dx * dx + dy * dy;

			float bossRadius = 64.0f;  // 128x128 보스 → 반지름 64
			float totalRadius = BREATH_RADIUS + bossRadius;

			if (distSq <= totalRadius * totalRadius) {
				float angleToBoss = atan2f(dy, dx);
				float angleDiff = fabsf(weaponAngle - angleToBoss);
				if (angleDiff > M_PI) angleDiff = 2 * M_PI - angleDiff;

				if (angleDiff <= M_PI / 6) {  // 60도 범위 내
					boss.hp -= weapons[WEAPON_BREATH].damage;
				}
			}
		}

		InvalidateRect(hwnd, NULL, FALSE);
		break;
	}

	case 14: { // 슬로우 무기 (원형 범위, 적 속도 감소)
		//weaponCoolTime(weapons[WEAPON_SLOW]);  // 쿨타임 및 활성 상태 갱신

		if (weapons[WEAPON_SLOW].active) {
			for (auto& enemy : enemies) {
				float dx = enemy.x - playerPos.x;
				float dy = enemy.y - playerPos.y;
				float distSq = dx * dx + dy * dy;

				if (distSq <= SLOW_RADIUS * SLOW_RADIUS) {
					enemy.slowUntil = GetTickCount() + 32;  // 2틱간 속도 절반
				}
			}
		}

		break;
	}
	case 15: { // 추적 무기
		DWORD now = GetTickCount();

		if (!homingWeapon.active && now - homingWeapon.lastAttackTime >= homingWeapon.cooldown) {
			int idx = FindBestTarget();
			if (idx != -1) {
				homingWeapon.active = true;
				homingWeapon.lastAttackTime = now;
				homingWeapon.targetIndex = idx;
				homingWeapon.x = playerPos.x;
				homingWeapon.y = playerPos.y;
			}
		}

		if (homingWeapon.active) {
			if (now - homingWeapon.lastAttackTime >= homingWeapon.duration || homingWeapon.targetIndex >= enemies.size()) {
				homingWeapon.active = false;
				break;
			}

			Enemy& target = enemies[homingWeapon.targetIndex];

			// 적이 제거된 경우
			if (target.hp <= 0) {
				homingWeapon.active = false;
				break;
			}

			float dx = target.x - homingWeapon.x;
			float dy = target.y - homingWeapon.y;
			float len = sqrtf(dx * dx + dy * dy);

			if (len > 0.1f) {
				homingWeapon.x += dx / len * homingWeapon.speed;
				homingWeapon.y += dy / len * homingWeapon.speed;
			}

			// 충돌 판정 (중심 간 거리)
			float distSq = (target.x - homingWeapon.x) * (target.x - homingWeapon.x) +
				(target.y - homingWeapon.y) * (target.y - homingWeapon.y);
			if (distSq <= 20 * 20) {  // 대략적 충돌 반경
				target.hp -= homingWeapon.damage;
				if (target.hp <= 0) {
					enemies.erase(enemies.begin() + homingWeapon.targetIndex);
					killCount++;

				}
				homingWeapon.active = false;
			}
		}
		break;
	}

	}
}

void DrawBoss3AttackEffect(HDC hdc)
{
	if (bossAttackStep > 0 && bossAttackStep <= bossAttackCount)
	{
		int currentRange = bossBaseRange + (bossAttackStep - 1) * bossRangeStep;

		// 붉은색 점선 원 (공격 범위)
		HPEN hPen = CreatePen(PS_DASH, 2, RGB(255, 0, 0));
		HPEN oldPen = (HPEN)SelectObject(hdc, hPen);

		HBRUSH hBrush = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
		HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);

		Ellipse(hdc,
			boss3.x - currentRange - cameraX,
			boss3.y - currentRange - cameraY,
			boss3.x + currentRange - cameraX,
			boss3.y + currentRange - cameraY);

		SelectObject(hdc, oldPen);
		SelectObject(hdc, oldBrush);
		DeleteObject(hPen);
	}
}

// 무기 타이머 활성화 상태 플래그
bool isBreathEnabled = false;
bool isSniperEnabled = false;
bool isCircleEnabled = false;
bool isSlowEnabled = false;
bool isHomingEnabled = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static HDC memDC;
	static HBITMAP memBM;
	static HBITMAP oldBM;

	switch (msg) {
	case WM_CREATE: {
		HDC hdc = GetDC(hwnd);
		memDC = CreateCompatibleDC(hdc);
		memBM = CreateCompatibleBitmap(hdc, WINDOW_WIDTH, WINDOW_HEIGHT);
		oldBM = (HBITMAP)SelectObject(memDC, memBM);
		ReleaseDC(hwnd, hdc);

		// === GDI+ 초기화 및 GIF 로드 ===
		GdiplusStartupInput gdiInput;
		GdiplusStartup(&gdiToken, &gdiInput, NULL);

		backgroundImg = new Bitmap(L"Resources/Map/MAP.png");
		menuBackgroundImg = new Bitmap(L"Resources/UI/mainmenu.png");
		playerGif.Load(L"Resources/Character/Player.gif");
		bossGif.Load(L"Resources/Character/Boss1.gif");
		boss2Gif.Load(L"Resources/Character/Boss2_flying.gif");
		boss3Gif.Load(L"Resources/Character/Boss3.gif");
		enemyNormalGif.Load(L"Resources/Character/enemyNormal.gif");
		enemyFastGif.Load(L"Resources/Character/enemyFast.gif");
		enemyTankGif.Load(L"Resources/Character/enemyTank.gif");
		slashGif.Load(L"Resources/Weapon/weapon_slash.gif");
		circleGif.Load(L"Resources/Weapon/weapon_circle.gif");
		breathGif.Load(L"Resources/Weapon/weapon_breath.gif");
		slowGif.Load(L"Resources/Weapon/weapon_slow.gif");
		cdGif.Load(L"Resources/Gimmick/CD.gif");
		fanGif.Load(L"Resources/Gimmick/wind.gif");
		LoadWeaponImages();

		//타입, 막공시간, 쿨타임, 유지시간, 공격여부, 데미지
		weapons[WEAPON_SLASH] = { WEAPON_SLASH, 0, 1000, 200, false, 3 };
		weapons[WEAPON_CIRCLE] = { WEAPON_CIRCLE, 0, 0, 0, false, 1 };
		weapons[WEAPON_BREATH] = { WEAPON_BREATH, 0, 5000, 2500, false, 2 };
		weapons[WEAPON_SLOW] = { WEAPON_SLOW, 0, 0, 0, false, 0 };
		killCount = 0;
		return 0;
	}

	case WM_KEYDOWN:
		switch (wParam) {
		case 'A':  keyLeft = true; break;
		case 'D': keyRight = true; break;
		case 'W':    keyUp = true; break;
		case 'S':  keyDown = true; break;
		case '1': debug = true; break;
		}
		if (currentState == GAME_START && wParam == VK_RETURN) {
			currentState = GAME_MAP_SELECT;
			InvalidateRect(hwnd, NULL, TRUE);
		}
		break;

	case WM_KEYUP:
		switch (wParam) {
		case 'A':  keyLeft = false; break;
		case 'D': keyRight = false; break;
		case 'W':    keyUp = false; break;
		case 'S':  keyDown = false; break;
		}
		break;
	case WM_LBUTTONUP:
		if (currentState == GAME_LOADING) {
			int x = LOWORD(lParam);
			int y = HIWORD(lParam);

			// WM_LBUTTONUP or WM_LBUTTONDOWN에서 좌표 확인
			if (x >= 100 && x <= 700) {
				if (y >= 150 && y <= 300) { // 첫 번째 버튼
					selectedMap = 1;
					currentState = GAME_PLAYING;
				}
				else if (y >= 340 && y <= 490) { // 두 번째 버튼
					selectedMap = 2;
					currentState = GAME_PLAYING2;
				}
				InvalidateRect(hwnd, NULL, TRUE);
			}

		}
		break;
	case WM_LBUTTONDOWN:
		if (currentState == GAME_MAP_SELECT) {
			gameStartTime = GetTickCount();
			int x = LOWORD(lParam);
			int y = HIWORD(lParam);

			if (x >= 100 && x <= 400) {
				if (y >= 150 && y <= 300) {
					currentState = GAME_LOADING;
				}
				else if (y >= 340 && y <= 490) {
					currentState = GAME_LOADING;
				}
				InvalidateRect(hwnd, NULL, TRUE);
			}
		}

		if (isChoosingWeapon) {
			int mx = LOWORD(lParam);
			int my = HIWORD(lParam);

			int boxWidth = 200;
			int boxHeight = 60;
			int gap = 40;
			int totalWidth = 3 * boxWidth + 2 * gap;
			int startX = (WINDOW_WIDTH - totalWidth) / 2;
			int y = WINDOW_HEIGHT / 2 - boxHeight / 2;

			for (int i = 0; i < 3; ++i) {
				int x = startX + i * (boxWidth + gap);
				RECT r = { x, y, x + boxWidth, y + boxHeight };

				if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
					selectedWeapon = i;
					int type = weaponChoices[i];

					// 무기 데미지 강화 or 활성화 처리
					switch (type) {
					case WEAPON_SLASH:
						// 강화: 쿨타임 감소 또는 데미지 증가
						weapons[WEAPON_SLASH].damage += 2;
						break;

					case WEAPON_BREATH:
						if (!isBreathEnabled) {
							SetTimer(hwnd, 13, 100, (TIMERPROC)TimerProc);  // 브레스 무기 충돌 체크 (0.1초마다)
							weapons[WEAPON_BREATH].damage = 5;
						}
						else {
							weapons[WEAPON_BREATH].damage++;
						}
						break;

					case WEAPON_CIRCLE:
						if (!isCircleEnabled) {
							SetTimer(hwnd, 12, 100, (TIMERPROC)TimerProc); // circle 타이머
							weapons[WEAPON_CIRCLE].active = true;
						}
						else {
							weapons[WEAPON_CIRCLE].damage++;
						}
						break;

					case WEAPON_SLOW:
						if (!isSlowEnabled) {
							SetTimer(hwnd, 14, 16, (TIMERPROC)TimerProc);  // 슬로우 무기 주기 (0.1초 간격)
							weapons[WEAPON_SLOW].active = true;
						}
						break;

					case WEAPON_COUNT:  // Homing Weapon
						if (!isHomingEnabled) {
							SetTimer(hwnd, 15, 16, (TIMERPROC)TimerProc);  // 스나이퍼 무기 약 60FPS
							homingWeapon.damage = 10;
						}
						else {
							homingWeapon.damage++;
						}
						break;
					}
					isChoosingWeapon = false;
					selectedWeapon = -1;
					break;
				}
			}
		}
		break;

	case WM_MOUSEMOVE:
		mousePos.x = GET_X_LPARAM(lParam);
		mousePos.y = GET_Y_LPARAM(lParam);
		break;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		// 카메라의 기본 위치는 플레이어 중심
		int offsetX = playerPos.x - WINDOW_WIDTH / 2;
		int offsetY = playerPos.y - WINDOW_HEIGHT / 2;
		switch (currentState) {
		case GAME_START:
			DrawStartScreen(memDC, hwnd);
			break;
		case GAME_MAP_SELECT:
			DrawMapSelectScreen(memDC, hwnd);
			break;
		case GAME_LOADING:
			SetTimer(hwnd, 1, 16, (TIMERPROC)TimerProc); // 이동 타이머
			SetTimer(hwnd, 2, 100, (TIMERPROC)TimerProc); // 충돌 타이머
			SetTimer(hwnd, 11, 16, (TIMERPROC)TimerProc); // slash 타이머
			break;
		case GAME_PLAYING:
			// 배경 그리기
			FillRect(memDC, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

			// 격자 그리기
			DrawGrid(memDC, offsetX, offsetY);

			break;
		case GAME_PLAYING2:
			// === 배경 이미지 출력 ===
			Graphics graphics(memDC);

			// 배경 이미지의 (cameraX, cameraY)~(cameraX+WINDOW_WIDTH, cameraY+WINDOW_HEIGHT) 부분만 화면에 그림
			Rect srcRect(cameraX, cameraY, WINDOW_WIDTH, WINDOW_HEIGHT);
			Rect destRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
			graphics.DrawImage(backgroundImg, destRect, srcRect.X, srcRect.Y, srcRect.Width, srcRect.Height, UnitPixel);
			break;
		}

		// 경계 처리
		if (offsetX < 0) offsetX = 0;
		if (offsetY < 0) offsetY = 0;
		if (offsetX > MAP_WIDTH - WINDOW_WIDTH) offsetX = MAP_WIDTH - WINDOW_WIDTH;
		if (offsetY > MAP_HEIGHT - WINDOW_HEIGHT) offsetY = MAP_HEIGHT - WINDOW_HEIGHT;

		// 화면 좌표에서 플레이어의 위치 계산
		int screenX = playerPos.x - offsetX;
		int screenY = playerPos.y - offsetY;

		// 플레이어 중심 좌표
		int centerX = playerPos.x - cameraX;
		int centerY = playerPos.y - cameraY;

		// 무기 중심 좌표 (플레이어 기준 앞쪽으로 offset만큼 이동)
		float weaponCenterX = centerX + cosf(weaponAngle) * slashOffset;
		float weaponCenterY = centerY + sinf(weaponAngle) * slashOffset;

		// 무기 꼭짓점 좌표 계산
		POINT weaponPoints[4];
		for (int i = 0; i < 4; i++) {
			float x = (i < 2 ? -WEAPON_WIDTH / 2.0f : WEAPON_WIDTH / 2.0f);
			float y = (i % 3 == 0 ? -WEAPON_HEIGHT / 2.0f : WEAPON_HEIGHT / 2.0f);

			// 회전 적용
			float rotatedX = x * cosf(weaponAngle) - y * sinf(weaponAngle);
			float rotatedY = x * sinf(weaponAngle) + y * cosf(weaponAngle);

			weaponPoints[i].x = (int)(weaponCenterX + rotatedX);
			weaponPoints[i].y = (int)(weaponCenterY + rotatedY);
		}

		// 무기 그리기
		DWORD now = GetTickCount();
		// === 무기 그리기 (GIF) ===
		for (int i = 0; i < WEAPON_COUNT; ++i) {
			Weapon& w = weapons[i];
			if (!w.active) continue;
			Graphics graphics(memDC);

			if (w.type == WEAPON_SLASH) {
				slashGif.NextFrame();
				slashGif.Draw(graphics,
					(int)(screenX + cosf(weaponAngle) * slashOffset - WEAPON_WIDTH / 2),
					(int)(screenY + sinf(weaponAngle) * slashOffset - WEAPON_HEIGHT / 2),
					WEAPON_WIDTH, WEAPON_HEIGHT, weaponAngle);
			}
			else if (w.type == WEAPON_CIRCLE) {
				circleGif.NextFrame();
				circleGif.Draw(graphics,
					(int)(playerPos.x - CIRCLE_RADIUS - cameraX),
					(int)(playerPos.y - CIRCLE_RADIUS - cameraY),
					(int)(CIRCLE_RADIUS * 2), (int)(CIRCLE_RADIUS * 2));
			}
			else if (w.type == WEAPON_BREATH) {
				breathGif.NextFrame();
				// 브레스는 플레이어 앞쪽에 출력
				breathGif.Draw(graphics,
					(int)(screenX + cosf(weaponAngle) * slashOffset - BREATH_RADIUS / 2),
					(int)(screenY + sinf(weaponAngle) * slashOffset - BREATH_RADIUS / 2),
					BREATH_RADIUS, BREATH_RADIUS, weaponAngle);
			}
			else if (w.type == WEAPON_SLOW) {
				slowGif.NextFrame();
				slowGif.Draw(graphics,
					(int)(playerPos.x - SLOW_RADIUS - cameraX),
					(int)(playerPos.y - SLOW_RADIUS - cameraY),
					(int)(SLOW_RADIUS * 2), (int)(SLOW_RADIUS * 2));
			}
		}
		if (homingWeapon.active) {
			int x = (int)(homingWeapon.x - cameraX);
			int y = (int)(homingWeapon.y - cameraY);

			HBRUSH hBrush = CreateSolidBrush(RGB(255, 0, 255));  // 보라색
			HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, hBrush);
			Ellipse(memDC, x - 10, y - 10, x + 10, y + 10);
			SelectObject(memDC, oldBrush);
			DeleteObject(hBrush);
		}


		// 적 그리기
		HBRUSH normalBrush = CreateSolidBrush(RGB(255, 0, 0));     // 빨간색 (기본)
		HBRUSH fastBrush = CreateSolidBrush(RGB(255, 128, 0));   // 주황색 (빠른 적)
		HBRUSH tankBrush = CreateSolidBrush(RGB(0, 128, 0));   // 노란색 (탱크형 적)

		for (auto& enemy : enemies) {
			int ex = enemy.x - cameraX;
			int ey = enemy.y - cameraY;
			Graphics graphics(memDC);
			if (enemy.type == ENEMY_FAST) {
				enemyFastGif.NextFrame();
				enemyFastGif.Draw(graphics, ex - 16, ey - 16, 32, 32);
			}
			else if (enemy.type == ENEMY_TANK) {
				enemyTankGif.NextFrame();
				enemyTankGif.Draw(graphics, ex - 16, ey - 16, 32, 32);
			}
			else {
				enemyNormalGif.NextFrame();
				enemyNormalGif.Draw(graphics, ex - 16, ey - 16, 32, 32);
			}
		}
		DeleteObject(normalBrush);
		DeleteObject(fastBrush);
		DeleteObject(tankBrush);

		for (const auto& b : bossBullets) {
			int sx = (int)(b.x - cameraX);
			int sy = (int)(b.y - cameraY);
			Ellipse(memDC, sx - 8, sy - 8, sx + 8, sy + 8);
		}

		DrawBoss3AttackEffect(memDC);

		// === 보스 그리기 (GIF) ===
		if (boss.active) {
			DrawBossAlert(memDC);  // memDC는 더블버퍼링
			int bx = boss.x - cameraX;
			int by = boss.y - cameraY;
			Graphics graphics(memDC);
			bossGif.NextFrame();
			bossGif.Draw(graphics, bx - 64, by - 64, 128, 128);
			boss.DrawHealthBar(memDC, WINDOW_WIDTH);
		}
		if (boss2.active) {
			DrawBossAlert(memDC);  // memDC는 더블버퍼링
			int bx = boss2.x - cameraX;
			int by = boss2.y - cameraY;
			Graphics graphics(memDC);
			boss2Gif.NextFrame();
			boss2Gif.Draw(graphics, bx - 64, by - 64, 128, 128);
			boss2.DrawHealthBar(memDC, WINDOW_WIDTH);
		}
		if (boss3.active) {
			DrawBossAlert(memDC);  // memDC는 더블버퍼링
			int bx = boss3.x - cameraX;
			int by = boss3.y - cameraY;
			Graphics graphics(memDC);
			boss3Gif.NextFrame();
			boss3Gif.Draw(graphics, bx - 64, by - 64, 128, 128);
			boss3.DrawHealthBar(memDC, WINDOW_WIDTH);
		}

		// 플레이어 그리기
		screenX = playerPos.x - cameraX;
		screenY = playerPos.y - cameraY;
		{
			Graphics graphics(memDC);
			playerGif.NextFrame();
			playerGif.Draw(graphics, screenX - 16, screenY - 16, 32, 32);
		}
		// === CD 기믹 그리기 (GIF) ===
		for (const auto& cd : cdGimmicks) {
			int cdX = (int)cd.x - cameraX;
			int cdY = (int)cd.y - cameraY;

			// 화면 내에 있을 때만 그리기
			if (cdX > -64 && cdX < WINDOW_WIDTH + 64 &&
				cdY > -64 && cdY < WINDOW_HEIGHT + 64) {

				Graphics graphics(memDC);
				cdGif.NextFrame();
				cdGif.Draw(graphics, cdX - (int)cd.radius, cdY - (int)cd.radius,
					(int)cd.radius * 2, (int)cd.radius * 2);
			}
		}

		// === 팬 기믹 그리기 (GIF) ===
		for (const auto& fan : fanGimmicks) {
			Graphics graphics(memDC);

			// 현재 바람 위치에 GIF 출력
			int drawX = (int)fan.x - cameraX;
			int drawY = (int)fan.y - cameraY;

			// 화면 내에 있을 때만 그리기
			if (drawX > -200 && drawX < WINDOW_WIDTH + 200 &&
				drawY > -200 && drawY < WINDOW_HEIGHT + 200) {

				fanGif.NextFrame();

				if (fan.windDx != 0) { // 수평 바람
					fanGif.Draw(graphics, drawX - 50, drawY - (int)fan.width,
						100, (int)fan.width * 2);
				}
				else { // 수직 바람
					fanGif.Draw(graphics, drawX - (int)fan.width, drawY - 50,
						(int)fan.width * 2, 100);
				}
			}
		}
		

		// 체력 표시
		if(currentState==GAME_PLAYING|| currentState == GAME_PLAYING2)
		{
			TCHAR text[128];
			DWORD elapsed = (GetTickCount() - gameStartTime) / 1000;

			SetTextColor(memDC, RGB(0, 0, 0));

			wsprintf(text, TEXT("레벨: %d"), playerLevel);
			TextOut(memDC, 10, 80, text, lstrlen(text));

			wsprintf(text, TEXT("플레이 시간: %d초"), elapsed);
			TextOut(memDC, 10, 110, text, lstrlen(text));

			DrawExpBar(memDC);

			DrawHealthBar(memDC);
		}

		DrawWeaponSelectionUI(memDC, WINDOW_WIDTH, WINDOW_HEIGHT);

		if (gameOver) {
			// === 배경 박스 ===
			const int boxWidth = 800;
			const int boxHeight = 300;
			int centerX = WINDOW_WIDTH / 2;
			int centerY = WINDOW_HEIGHT / 2;

			RECT gameOverBox = {
				centerX - boxWidth / 2,
				centerY - boxHeight / 2,
				centerX + boxWidth / 2,
				centerY + boxHeight / 2
			};

			// 검은 배경 박스
			HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
			FillRect(memDC, &gameOverBox, blackBrush);
			DeleteObject(blackBrush);

			// 텍스트 설정
			SetBkMode(memDC, TRANSPARENT);
			SetTextColor(memDC, RGB(255, 0, 0));

			HFONT hFont = CreateFont(40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
			HFONT oldFont = (HFONT)SelectObject(memDC, hFont);

			// 텍스트 출력
			DrawText(memDC, L"GAME OVER", -1, &gameOverBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

			// 정리
			SelectObject(memDC, oldFont);
			DeleteObject(hFont);
		}

		if (killedBoss3) {
			Image* endingImage = new Image(L"Resources/UI/patched.png"); // 전역 또는 상태 관리용
			Graphics g(memDC);
			g.DrawImage(endingImage, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
		}

		// 최종 출력
		BitBlt(hdc, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, memDC, 0, 0, SRCCOPY);

		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_DESTROY:
		// === GIF 메모리 해제 및 GDI+ 종료 ===
		if (backgroundImg) delete backgroundImg;
		if (menuBackgroundImg) delete menuBackgroundImg;
		playerGif.Release();
		bossGif.Release();
		boss2Gif.Release();
		boss3Gif.Release();
		enemyNormalGif.Release();
		enemyFastGif.Release();
		enemyTankGif.Release();
		cdGif.Release();
		fanGif.Release();
		ReleaseWeaponImages();
		GdiplusShutdown(gdiToken);
		SelectObject(memDC, oldBM);
		DeleteObject(memBM);
		DeleteDC(memDC);
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}
