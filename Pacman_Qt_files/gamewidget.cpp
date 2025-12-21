#include "gamewidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRandomGenerator>
#include <cmath>
#include <QMouseEvent>
#include <QUrl>

#define MOVESTEPS 6
#define FRAMETIME 40
#define REPRODUCTION_PROB 5
#define MAX_GHOSTS 13
// === CONSTRUCTOR ===
GameWidget::GameWidget(QWidget *parent)
    : QWidget(parent),
    m_gameState(Menu),
    m_score(0),
    m_round(1), // <-- Default start round
    m_pacmanMouthAngle(10),       // Start with mouth slightly open
    m_pacmanMouthDirection(1),    // Mouth will open
    m_pacmanAnimationCounter(0),
    m_pacmanDirection(Right),
    startMacroRow(-1),
    startMacroCol(-1), // 8 animation steps per tile
    isMoving(false),
    moveSteps(MOVESTEPS),
    currentStep(0),
    nextGhostId(0),
    m_isPixelatedMode(false),
    tcpServer(nullptr),
    clientSocket(nullptr)
{
    m_zoomFactor = 1.0f;
    // Set the window size based on our tile grid
    const int BOTTOM_BAR_HEIGHT = 60;
    setFixedSize(
        LEFT_SIDEBAR_WIDTH + MAZE_WIDTH * TILE_SIZE + RIGHT_SIDEBAR_WIDTH,
        MAZE_HEIGHT * TILE_SIZE + BOTTOM_BAR_HEIGHT
        );



    // Compute left sidebar button rectangles (assuming left margin for controls)
    int btnCount = 7;
    int btnWidth = 38, btnHeight = 38;
    int spacing = 14;
    int totalBtnHeight = btnCount * btnHeight + (btnCount - 1) * spacing;
    int topOffset = (height() - totalBtnHeight) / 2;

    m_roundBtnRects.clear();
    for (int i = 0; i < btnCount; ++i) {
        int y = topOffset + i * (btnHeight + spacing);
        m_roundBtnRects.append(QRect(
            (LEFT_SIDEBAR_WIDTH - btnWidth) / 2, // Centered in left sidebar
            y, btnWidth, btnHeight));
    }

    // Place Win button at top of left sidebar, above round buttons
    int winBtnX = LEFT_SIDEBAR_WIDTH / 2 - btnWidth / 2;
    int winBtnY = 24; // Some margin at the top
    m_winSidebarBtnRect = QRect(winBtnX, winBtnY, btnWidth, btnHeight);


    // Load all sprites from the resource file
    loadAssets();

    // Load intro and game over images
    m_introImage.load(":/assets/intro_screen.png");
    m_gameOverImage.load(":/assets/game_over_screen.png");

    // Initialize Try Again button for GameOver state
    int tryAgainWidth = 220;
    int tryAgainHeight = 50;
    int centerX = width() / 2;
    m_tryAgainButtonRect = QRect(centerX - (tryAgainWidth / 2), height() - 100, tryAgainWidth, tryAgainHeight);
    // Load win image
    m_winImage.load(":/assets/win.jpg");

    // Initialize Next Round button for Win state
    int nextRoundWidth = 220;
    int nextRoundHeight = 50;
    m_nextRoundButtonRect = QRect(centerX - (nextRoundWidth / 2), height() - 100, nextRoundWidth, nextRoundHeight);


    // Update m_startButtonRect for consistency
    m_startButtonRect = m_tryAgainButtonRect;

    // Setup 12 distinct colors (yellow is first for Pac-Man)
    m_colorBtnColors = {
        QColor(255,255,0),    // yellow
        QColor(255,0,0),      // red
        QColor(0,0,255),      // blue
        QColor(0,255,0),      // green
        QColor(255,0,255),    // magenta
        QColor(0,255,255),    // cyan
        QColor(255,165,0),    // orange
        QColor(128,0,128),    // purple
        QColor(0,128,0),      // dark green
        QColor(128,128,128),  // gray
        QColor(255,192,203),  // pink
        QColor(139,69,19)     // brown
    };
    m_pacmanColorIdx = 0;

    // Calculate bottom bar button rects (outside frame)
    int barHeight = 60;
    int buttonWidth = 44;
    int buttonHeight = 44;
    spacing = 18;
    int totalWidth = 12 * buttonWidth + 11 * spacing;
    int barY = height() - barHeight; // Align at the bottom within widget
    int barX0 = (width() - totalWidth) / 2;

    m_colorBtnRects.clear();
    for (int i = 0; i < 12; ++i) {
        QRect rect(barX0 + i*(buttonWidth + spacing), barY + (barHeight - buttonHeight) / 2, buttonWidth, buttonHeight);
        m_colorBtnRects.append(rect);
    }



    // Load the maze map from resources
    loadMaze(":/assets/map.txt");

    // Porting your panic timer
    panicTimer = new QTimer(this);
    panicTimer->setSingleShot(true);
    connect(panicTimer, &QTimer::timeout, this, &GameWidget::panicModeTimeout);

    // Start the main game loop timer (ticks every ~33ms)
    m_gameTimerId = startTimer(FRAMETIME);

    setFocusPolicy(Qt::StrongFocus);

    // --- UPDATE BUTTON LAYOUT ---
    int startY = height() / 2 - 60;

    // Button 1: High Res
    m_highResBtnRect = QRect(centerX - (buttonWidth / 2), startY, buttonWidth, buttonHeight);

    // Button 2: Pixelated (placed below Button 1)
    m_pixelBtnRect = QRect(centerX - (buttonWidth / 2), startY + 70, buttonWidth, buttonHeight);

    // Move Level Selection Buttons further down
    int btnX = centerX - 50;
    int btnY = startY + 150;
    m_levelDownRect = QRect(btnX - 40, btnY, 30, 30);
    m_levelUpRect = QRect(btnX + 60, btnY, 30, 30);
    // ----------------------------------------------------------------------


    // === ADDED: Set up level selection buttons ===
    // int btnX = (width() / 2) - 50;
    // int btnY = m_startButtonRect.y() + 70; // Below the start button
    m_levelDownRect = QRect(btnX - 40, btnY, 30, 30);
    m_levelUpRect = QRect(btnX + 60, btnY, 30, 30);
    // =============================================

    // === ADD THIS: INITIALIZE AUDIO ===
    // 1. Background Music
    m_bgMusicPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this); // Create the "speaker"

    m_bgMusicPlayer->setAudioOutput(m_audioOutput); // Connect player to speaker
    m_bgMusicPlayer->setSource(QUrl("qrc:/assets/bg_music.mp3")); // Set the song

    // Set volume on the OUTPUT, not the player.
    // 0.0 is silent, 1.0 is full volume.
    m_audioOutput->setVolume(0.5);

    m_bgMusicPlayer->setLoops(QMediaPlayer::Infinite);

    // 2. Pellet SFX
    m_pelletSfx = new QSoundEffect(this);
    m_pelletSfx->setSource(QUrl("qrc:/assets/pellet.wav"));
    m_pelletSfx->setVolume(0.5f); // Pellet sounds can be loud, 0.5 = 50% volume

    // 3. Power Pellet SFX
    m_powerPelletSfx = new QSoundEffect(this);
    m_powerPelletSfx->setSource(QUrl("qrc:/assets/power_pellet.wav"));
    m_powerPelletSfx->setVolume(0.5f);
    // 4. Game Over SFX
    m_gameOverSfx = new QSoundEffect(this);
    m_gameOverSfx->setSource(QUrl("qrc:/assets/game_over.wav"));
    // =================================
    initializeSocketServer();
}

GameWidget::~GameWidget()
{
    // Clean up socket connections
    if (clientSocket) {
        clientSocket->close();
        clientSocket->deleteLater();
    }

    if (tcpServer) {
        tcpServer->close();
        tcpServer->deleteLater();
    }
}


void GameWidget::loadAssets()
{
    if (m_isPixelatedMode) {
        m_wallSprite.load(":/assets/pixel_wall.png");
        m_pelletSprite.load(":/assets/pixel_pellet.png");
        m_powerPelletSprite.load(":/assets/pixel_power_pellet.png");
        m_emptySprite.load(":/assets/pixel_empty.png");
    } else {
        m_wallSprite.load(":/assets/wall.png");
        m_pelletSprite.load(":/assets/pellet.png");
        m_powerPelletSprite.load(":/assets/power_pellet.png");
        m_emptySprite.load(":/assets/empty.png");
    }

    // Load intro and game over images
    m_introImage.load(":/assets/intro_screen.png");
    m_winImage.load(":/assets/win.png");
    m_gameOverImage.load(":/assets/game_over_screen.png");
}


// === MAIN EVENT HANDLERS ===

void GameWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_gameTimerId) {
        switch (m_gameState) {
        case Menu:
            // No update needed
            break;
        case Playing:
            updateGame(); // Run all game logic
            break;
        case Win:
            // No update needed
            break;
        case GameOver:
            // No update needed
            break;
        }

        // Tell Qt to redraw the screen. This will call paintEvent().
        update();
    }
}

void GameWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // --- DYNAMIC RENDER HINT ---
    if (m_isPixelatedMode) {
        // Disable antialiasing for that retro pixel look
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    } else {
        // Enable smooth drawing for HD
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    }

    switch (m_gameState) {
    case Menu:
        drawMenu(painter);
        break;
    case Playing:
        drawGame(painter);
        break;
    case Win:
        drawWin(painter);
        break;
    case GameOver:
        drawGameOver(painter);
        break;
    }
}

void GameWidget::initializeSocketServer()
{
    tcpServer = new QTcpServer(this);
    clientSocket = nullptr;

    connect(tcpServer, &QTcpServer::newConnection, this, &GameWidget::onNewConnection);

    if (!tcpServer->listen(QHostAddress::Any, 12345)) {
        qDebug() << "Server could not start on port 12345!";
    } else {
        qDebug() << "Server started on port 12345. Waiting for head pose client...";
    }
}


void GameWidget::onNewConnection()
{
    clientSocket = tcpServer->nextPendingConnection();
    qDebug() << "Head pose client connected:" << clientSocket->peerAddress().toString();

    connect(clientSocket, &QTcpSocket::readyRead, this, &GameWidget::onReadyRead);
    connect(clientSocket, &QTcpSocket::disconnected, this, &GameWidget::onClientDisconnected);
}


void GameWidget::onClientDisconnected()
{
    qDebug() << "Head pose client disconnected";

    if (clientSocket) {
        clientSocket->deleteLater();
        clientSocket = nullptr;
    }
}

void GameWidget::onReadyRead()
{
    if (!clientSocket) return;

    QByteArray data = clientSocket->readAll();
    QString command = QString::fromUtf8(data).trimmed();

    qDebug() << "Received command:" << command;
    processMovementCommand(command);
}


void GameWidget::processMovementCommand(const QString &command)
{
    // FIXED: Check if game is NOT Playing
    if (m_gameState != Playing || isMoving) {
        return; // Ignore commands if game not playing or already moving
    }

    // Ignore "Center" or commands with ":"
    if (command == "Center" || command.contains(":")) {
        return;
    }

    // Process single direction commands
    if (command == "Up") {
        if (canMove(0, -1)) {  // FIXED: Should be -1, not 7
            m_pacmanDirection = Up;
            startAnimatedMove(0, -TILE_SIZE);  // FIXED: Use TILE_SIZE, not 7
        }
    } else if (command == "Down") {
        if (canMove(0, 1)) {  // FIXED: Should be 1, not -7
            m_pacmanDirection = Down;
            startAnimatedMove(0, TILE_SIZE);  // FIXED: Use TILE_SIZE, not -7
        }
    } else if (command == "Left") {
        if (canMove(-1, 0)) {  // FIXED: Should be -1, not -7
            m_pacmanDirection = Left;
            startAnimatedMove(-TILE_SIZE, 0);  // FIXED: Use -TILE_SIZE, not -7
        }
    } else if (command == "Right") {
        if (canMove(1, 0)) {  // FIXED: Should be 1, not 7
            m_pacmanDirection = Right;
            startAnimatedMove(TILE_SIZE, 0);  // FIXED: Use TILE_SIZE, not 7
        }
    }
}

void GameWidget::keyPressEvent(QKeyEvent *event)
{
    // FIXED: Check if game is NOT Playing
    if (m_gameState != Playing || isMoving) {
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        if (canMove(0, -1)) {  // FIXED: macro grid delta
            m_pacmanDirection = Up;
            startAnimatedMove(0, -TILE_SIZE);  // FIXED: pixel delta
        }
        break;
    case Qt::Key_Down:
        if (canMove(0, 1)) {  // FIXED
            m_pacmanDirection = Down;
            startAnimatedMove(0, TILE_SIZE);  // FIXED
        }
        break;
    case Qt::Key_Left:
        if (canMove(-1, 0)) {  // FIXED
            m_pacmanDirection = Left;
            startAnimatedMove(-TILE_SIZE, 0);  // FIXED
        }
        break;
    case Qt::Key_Right:
        if (canMove(1, 0)) {  // FIXED
            m_pacmanDirection = Right;
            startAnimatedMove(TILE_SIZE, 0);  // FIXED
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void GameWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_gameState == Playing) {
        for (int i = 0; i < m_colorBtnRects.size(); ++i) {
            if (m_colorBtnRects[i].contains(event->pos())) {
                m_pacmanColorIdx = i;
                update();
                return;
            }
        }
    }

    // Only allow when not in menu
    if (m_gameState == Playing || m_gameState == GameOver || m_gameState == Win) {
        for (int i = 0; i < m_roundBtnRects.size(); ++i) {
            if (m_roundBtnRects[i].contains(event->pos())) {
                m_round = i + 1;
                startGame();
                update();
                return;
            }
        }

        // Handle zoom buttons
        if (m_zoomInRect.contains(event->pos())) {
            m_zoomFactor = qMin(2.0f, m_zoomFactor + 0.1f);
            update();
            return;
        }

        if (m_zoomOutRect.contains(event->pos())) {
            m_zoomFactor = qMax(0.5f, m_zoomFactor - 0.1f);
            update();
            return;
        }
    }

    // Win sidebar button: transition to Win state (show Win screen)
    if (m_winSidebarBtnRect.contains(event->pos())) {
        m_gameState = Win;
        update();
        return;
    }

    // --- Menu state logic ---
    if (m_gameState == Menu) {
        if (m_highResBtnRect.contains(event->pos())) {
            m_isPixelatedMode = false;
            loadAssets();
            startGame();
        } else if (m_pixelBtnRect.contains(event->pos())) {
            m_isPixelatedMode = true;
            loadAssets();
            startGame();
        } else if (m_levelDownRect.contains(event->pos())) {
            if (m_round > 1) m_round--;
            update();
        } else if (m_levelUpRect.contains(event->pos())) {
            if (m_round < 7) m_round++;
            update();
        }
    }
    // --- Win state logic: Next Round button advances to next round ---
    else if (m_gameState == Win && m_nextRoundButtonRect.contains(event->pos())) {
        // Increment to next round
        if (m_round < 7) {
            m_round++;
        } else {
            // If already at max round, restart from round 1
            m_round = 1;
        }
        startGame();
    }
    // --- GameOver state logic: Try Again button restarts from current round ---
    else if (m_gameState == GameOver && m_tryAgainButtonRect.contains(event->pos())) {
        // Keep the current m_round and restart the game
        startGame();
    }
}


// === STATE-SPECIFIC UPDATE FUNCTIONS ===

void GameWidget::updateGame()
{
    // 1. Run Pac-Man's animation (if he's moving)
    if (isMoving) {
        animationStep();

        // Animate Pac-Man's mouth
        m_pacmanAnimationCounter++;
        if (m_pacmanAnimationCounter > 2) { // Change every 3 frames
            m_pacmanAnimationCounter = 0;
            m_pacmanMouthAngle += m_pacmanMouthDirection * 15;
            // Reverse direction if mouth is fully open or closed
            if (m_pacmanMouthAngle >= 45 || m_pacmanMouthAngle <= 0) {
                m_pacmanMouthDirection *= -1;
            }
        }
    }

    // 2. Run the Ghost's animation/AI step
    ghostAnimationStep();

    // 3. Check for collisions
    checkGhostCollisions();
}


// === STATE-SPECIFIC DRAWING FUNCTIONS ===

void GameWidget::drawMenu(QPainter &painter)
{
    painter.fillRect(rect(), Qt::black);

    // Draw the new intro image scaled to fit frame
    if (!m_introImage.isNull()) {
        QRect imageRect = rect();
        painter.drawPixmap(imageRect, m_introImage);
    }

    // Placement parameters for bottom alignment
    int buttonWidth = 220;
    int buttonHeight = 50;
    int buttonSpacing = 30;
    int centerX = width() / 2;
    int bottomMargin = 40; // Tighter margin to bottom

    // Start buttons: left and right of center, side by side, near bottom
    int buttonY = height() - bottomMargin - buttonHeight - 40; // 40px above very bottom
    m_highResBtnRect = QRect(centerX - buttonWidth - buttonSpacing/2, buttonY, buttonWidth, buttonHeight);
    m_pixelBtnRect   = QRect(centerX + buttonSpacing/2, buttonY, buttonWidth, buttonHeight);

    // Level selection buttons: centered below start buttons
    int levelButtonY = buttonY + buttonHeight + 16;
    int btnX = centerX - 50;
    m_levelDownRect = QRect(btnX - 40, levelButtonY, 30, 30);
    m_levelUpRect   = QRect(btnX + 60, levelButtonY, 30, 30);

    // --- DRAW HIGH RES BUTTON ---
    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::cyan);
    painter.drawRect(m_highResBtnRect);
    painter.setPen(Qt::black);
    QFont font("Arial", 20, QFont::Bold);
    painter.setFont(font);
    painter.drawText(m_highResBtnRect, Qt::AlignCenter, "START (HD)");

    // --- DRAW PIXEL BUTTON ---
    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::magenta);
    painter.drawRect(m_pixelBtnRect);
    painter.setPen(Qt::black);
    painter.drawText(m_pixelBtnRect, Qt::AlignCenter, "START (PIXEL)");

    // === Draw level selection ===
    font.setPointSize(18);
    painter.setFont(font);
    painter.setPen(Qt::white);

    // Draw the text "Round: X"
    QString roundText = QString("Round: %1").arg(m_round);
    QFontMetrics fm(painter.font());
    int textWidth = fm.horizontalAdvance(roundText);
    QRect roundTextRect(m_levelDownRect.right() + 10,
                        m_levelDownRect.y(),
                        textWidth + 20,
                        30);

    m_levelUpRect.moveLeft(roundTextRect.right() + 10);

    painter.drawText(roundTextRect, Qt::AlignCenter, roundText);

    // Draw '-' button
    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::yellow);
    painter.drawRect(m_levelDownRect);
    painter.setPen(Qt::black);
    font.setPointSize(20);
    painter.setFont(font);
    painter.drawText(m_levelDownRect, Qt::AlignCenter, "-");

    // Draw '+' button
    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::yellow);
    painter.drawRect(m_levelUpRect);
    painter.setPen(Qt::black);
    painter.drawText(m_levelUpRect, Qt::AlignCenter, "+");
}

void GameWidget::drawWin(QPainter &painter)
{
    painter.fillRect(rect(), Qt::black);

    // Draw the win image scaled to fit the frame
    if (!m_winImage.isNull()) {
        QRect imageRect = rect();
        painter.drawPixmap(imageRect, m_winImage);
    }

    // Display the score at the top-center area
    painter.setPen(Qt::yellow);
    QFont scoreFont("Arial", 32, QFont::Bold);
    painter.setFont(scoreFont);
    QRect scoreRect = rect().adjusted(0, 80, 0, 0);
    painter.drawText(scoreRect, Qt::AlignHCenter | Qt::AlignTop, QString("Score: %1").arg(m_score));

    // Calculate Next Round button position - bottom aligned
    int buttonWidth = 220;
    int buttonHeight = 50;
    int centerX = width() / 2;
    int bottomMargin = 80;
    m_nextRoundButtonRect = QRect(centerX - (buttonWidth / 2), height() - bottomMargin, buttonWidth, buttonHeight);

    // Draw Next Round button
    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::green);
    painter.drawRect(m_nextRoundButtonRect);
    painter.setPen(Qt::black);
    QFont btnFont("Arial", 20, QFont::Bold);
    painter.setFont(btnFont);
    painter.drawText(m_nextRoundButtonRect, Qt::AlignCenter, "NEXT ROUND");
}


void GameWidget::drawGame(QPainter &painter)
{
    // Fill entire widget
    painter.fillRect(rect(), Qt::black);

    // --- Draw game field offset by LEFT_SIDEBAR_WIDTH ---
    painter.save();
    painter.translate(LEFT_SIDEBAR_WIDTH, 0); // Offset for left sidebar
    painter.scale(m_zoomFactor, m_zoomFactor);

    // Draw maze, Pac-Man, ghosts, pellets, etc...
    for (int row = 0; row < MAZE_HEIGHT; row++) {
        for (int col = 0; col < MAZE_WIDTH; col++) {
            int x = col * TILE_SIZE;
            int y = row * TILE_SIZE;

            painter.drawPixmap(x, y, m_emptySprite);

            if (mazeGrid[row][col] == 1) {
                if (m_isPixelatedMode) {
                    painter.setBrush(QColor(0, 0, 255));
                    painter.setPen(Qt::NoPen);
                    painter.drawRect(x, y, TILE_SIZE, TILE_SIZE);
                } else {
                    painter.drawPixmap(x, y, m_wallSprite);
                }
            }

            if (mazeGrid[row][col] == 0) {
                painter.drawPixmap(x, y, m_pelletSprite);
            }

            if (mazeGrid[row][col] == 4) {
                painter.drawPixmap(x, y, m_powerPelletSprite);
            }
        }
    }

    drawPacman(painter, pacman_grid_center, m_pacmanDirection);

    for (const Ghost &ghost : ghosts) {
        if (ghost.active) {
            drawGhost(painter, ghost);
        }
    }

    painter.restore();

    // --- Draw Left Sidebar Buttons (Virtual Round Select) ---
    if (m_gameState == Playing || m_gameState == GameOver || m_gameState == Win) {
        QFont btnFont("Arial", 18, QFont::Bold);
        painter.setFont(btnFont);

        for (int i = 0; i < m_roundBtnRects.size(); ++i) {
            QRect rect = m_roundBtnRects[i];
            if (m_round == (i + 1)) {
                painter.setBrush(Qt::yellow);
                painter.setPen(QColor(0, 0, 255));
            } else {
                painter.setBrush(Qt::white);
                painter.setPen(QColor(0, 0, 255));
            }
            painter.drawRect(rect);
            painter.setPen(Qt::black);
            painter.drawText(rect, Qt::AlignCenter, QString::number(i + 1));
        }
    }

    // Draw Win Button at top of left sidebar
    painter.setPen(QColor(0, 180, 0));
    painter.setBrush(Qt::green);
    painter.drawRect(m_winSidebarBtnRect);
    painter.setPen(Qt::black);
    QFont winFont("Arial", 12, QFont::Bold);
    painter.setFont(winFont);
    painter.drawText(m_winSidebarBtnRect, Qt::AlignCenter, "Win");


    // --- Draw Right Sidebar (Zoom Buttons - unchanged) ---
    int uiPaneStart = LEFT_SIDEBAR_WIDTH + MAZE_WIDTH * TILE_SIZE;
    int btnMargin = 16;
    int btnSize = 38;

    m_zoomInRect = QRect(uiPaneStart + btnMargin, height()/2 - btnSize - 8, btnSize, btnSize);
    m_zoomOutRect = QRect(uiPaneStart + btnMargin, height()/2 + 8, btnSize, btnSize);

    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::white);
    painter.drawRect(m_zoomInRect);
    painter.drawRect(m_zoomOutRect);

    QFont zoomFont("Arial", 18, QFont::Bold);
    painter.setFont(zoomFont);
    painter.setPen(Qt::black);
    painter.drawText(m_zoomInRect, Qt::AlignCenter, "+");
    painter.drawText(m_zoomOutRect, Qt::AlignCenter, "-");

    // Draw bottom bar/buttons - exclude from zoom/translation!
    if (m_gameState == Playing) {
        for (int i = 0; i < 12; ++i) {
            QRect rect = m_colorBtnRects[i];
            painter.setPen(Qt::black);
            painter.setBrush(m_colorBtnColors[i]);
            painter.drawRect(rect);
            if (i == m_pacmanColorIdx) {
                painter.setPen(QPen(Qt::white, 4));
                painter.drawRect(rect.adjusted(-2,-2,2,2));
            }
        }
    }


}

void GameWidget::drawGameOver(QPainter &painter)
{
    painter.fillRect(rect(), Qt::black);

    // Draw the game over image scaled to fit the frame
    if (!m_gameOverImage.isNull()) {
        QRect imageRect = rect();
        painter.drawPixmap(imageRect, m_gameOverImage);
    }

    // Display the score at the top-center area
    painter.setPen(Qt::yellow);
    QFont scoreFont("Arial", 32, QFont::Bold);
    painter.setFont(scoreFont);
    QRect scoreRect = rect().adjusted(0, 80, 0, 0);
    painter.drawText(scoreRect, Qt::AlignHCenter | Qt::AlignTop, QString("Score: %1").arg(m_score));

    // Calculate Try Again button position - bottom aligned
    int buttonWidth = 220;
    int buttonHeight = 50;
    int centerX = width() / 2;
    int bottomMargin = 80;
    m_tryAgainButtonRect = QRect(centerX - (buttonWidth / 2), height() - bottomMargin, buttonWidth, buttonHeight);

    // Draw Try Again button
    painter.setPen(QColor(0, 0, 255));
    painter.setBrush(Qt::yellow);
    painter.drawRect(m_tryAgainButtonRect);
    painter.setPen(Qt::black);
    QFont btnFont("Arial", 24, QFont::Bold);
    painter.setFont(btnFont);
    painter.drawText(m_tryAgainButtonRect, Qt::AlignCenter, "TRY AGAIN");
}

void GameWidget::drawPacman(QPainter &painter, QPoint center, Direction dir)
{
    // === 1. SETUP: Determine drawing target and size ===
    QPainter *drawTarget = &painter;
    QPixmap buffer;
    int targetSize = TILE_SIZE;
    int bufferResolution = TILE_SIZE;

    // Only use the buffer for pixelated mode
    if (m_isPixelatedMode) {
        // Set the internal drawing resolution (e.g., 8x8)
        bufferResolution = TILE_SIZE / 4;
        buffer = QPixmap(bufferResolution, bufferResolution);
        buffer.fill(Qt::transparent); // Start with transparent background

        drawTarget = new QPainter(&buffer); // Draw onto the buffer
        drawTarget->setRenderHint(QPainter::Antialiasing, false);

    }

    // === 2. ACTUAL PACMAN DRAWING LOGIC (Modified to use drawTarget) ===
    drawTarget->setPen(Qt::NoPen);
    drawTarget->setBrush(m_colorBtnColors[m_pacmanColorIdx]);

    int angle = m_pacmanMouthAngle * 16;
    int span = (360 - m_pacmanMouthAngle * 2) * 16;
    int startAngle = 0;

    // Calculate the size and center for the target resolution (bufferResolution)
    int radius = bufferResolution / 2;
    QRect rect1(center.x()-bufferResolution/2,center.y()-bufferResolution/2, bufferResolution, bufferResolution);
    QRect rect(0,0, bufferResolution, bufferResolution);

    // Rotate the mouth based on direction
    switch (dir) {
    case Right: startAngle = angle / 2; break;
    case Left:  startAngle = (180 * 16) + (angle / 2); break;
    case Up:    startAngle = (90 * 16) + (angle / 2); break;
    case Down:  startAngle = (270 * 16) + (angle / 2); break;
    default:    startAngle = angle / 2; break;
    }



    // === 3. FINAL STEP: Draw the buffer if necessary ===
    if (m_isPixelatedMode) {
        drawTarget->drawPie(rect, startAngle, span);
        delete drawTarget; // Finish painting on the buffer
        // Draw the low-res buffer onto the main painter, scaling it up to 32x32.
        // This scaling creates the blocky, pixelated look.
        painter.drawPixmap(center.x() - targetSize/2, center.y() - targetSize/2, targetSize, targetSize, buffer);
    }
    else drawTarget->drawPie(rect1, startAngle, span);
    // If not in pixelated mode, it was drawn directly to 'painter'.
}

// This function draws the ghost using an 8x8 buffer, which is then scaled up to 32x32 (TILE_SIZE),
// forcing a blocky, low-resolution appearance.
void GameWidget::drawPixelGhost(QPainter &painter, const Ghost &ghost)
{
    const int bufferScale = 4;
    const int bufferResolution = TILE_SIZE / bufferScale; // E.g., 8x8
    QPixmap buffer(bufferResolution, bufferResolution);
    buffer.fill(Qt::transparent);
    QPainter drawTarget(&buffer);
    drawTarget.setRenderHint(QPainter::Antialiasing, false);

    // Body - colored circle
    QColor ghostCol = (ghost.mode == Panic)
                          ? QColor(0, 100, 255)
                          : QColor(ghost.color.r, ghost.color.g, ghost.color.b);
    drawTarget.setBrush(ghostCol);
    drawTarget.setPen(Qt::NoPen);
    drawTarget.drawEllipse(buffer.rect());

    // Eyes - simple white dots
    drawTarget.setBrush(Qt::white);
    int eyeR = bufferResolution / 6;
    QPoint leftEye(bufferResolution / 3, bufferResolution / 3);
    QPoint rightEye(2 * bufferResolution / 3, bufferResolution / 3);
    drawTarget.drawEllipse(leftEye, eyeR, eyeR);
    drawTarget.drawEllipse(rightEye, eyeR, eyeR);

    // Pupils - centered black dots
    drawTarget.setBrush(Qt::black);
    int pupilR = bufferResolution / 12;
    drawTarget.drawEllipse(leftEye, pupilR, pupilR);
    drawTarget.drawEllipse(rightEye, pupilR, pupilR);

    // Scale up and draw at ghost position
    int px = ghost.grid_center.x() - TILE_SIZE / 2;
    int py = ghost.grid_center.y() - TILE_SIZE / 2;
    painter.drawPixmap(px, py, TILE_SIZE, TILE_SIZE, buffer);
}


void GameWidget::drawGhost(QPainter &painter, const Ghost &ghost)
{
    if (m_isPixelatedMode) {
        drawPixelGhost(painter, ghost);
        return;
    }

    QPoint center = ghost.grid_center;
    int r = TILE_SIZE / 2;

    // Set the ghost's color based on its mode
    if (ghost.mode == Panic) {
        painter.setBrush(QColor(0, 100, 255)); // Blue panic color
    } else {
        // Use the color from your original logic!
        painter.setBrush(QColor(ghost.color.r, ghost.color.g, ghost.color.b));
    }
    painter.setPen(Qt::NoPen);

    // Use a QPainterPath to create the ghost shape
    QPainterPath path;

    // Top semi-circle
    QRectF head(center.x() - r, center.y() - r, TILE_SIZE, TILE_SIZE);
    path.arcMoveTo(head, 180);
    path.arcTo(head, 180, -180); // Arc from left to right over the top

    // Wavy bottom
    int numWaves = 3;
    float waveWidth = (float)TILE_SIZE / numWaves;
    float waveHeight = TILE_SIZE / 6;

    QPointF current = path.currentPosition(); // Should be (center.x + r, center.y)

    for (int i = 0; i < numWaves; ++i) {
        QPointF p1 = QPointF(current.x() - (waveWidth / 2.0), current.y() + waveHeight);
        QPointF p2 = QPointF(current.x() - waveWidth, current.y());
        path.quadTo(p1, p2);
        current = p2;
    }

    path.closeSubpath(); // Connects back to (center.x - r, center.y)
    painter.drawPath(path);

    // --- Draw Eyes ---
    painter.setBrush(Qt::white);
    int eyeR = TILE_SIZE / 6;
    QPoint leftEyeCenter(center.x() - TILE_SIZE/4, center.y() - TILE_SIZE/6);
    QPoint rightEyeCenter(center.x() + TILE_SIZE/4, center.y() - TILE_SIZE/6);

    painter.drawEllipse(leftEyeCenter, eyeR, eyeR);
    painter.drawEllipse(rightEyeCenter, eyeR, eyeR);

    if (ghost.mode == Panic)
    {
        // Simple scared mouth
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::NoBrush);
        QPainterPath mouth;
        mouth.moveTo(center.x() - TILE_SIZE/4, center.y() + TILE_SIZE/4);
        mouth.quadTo(center.x(), center.y() + TILE_SIZE/8,
                     center.x() + TILE_SIZE/4, center.y() + TILE_SIZE/4);
        painter.drawPath(mouth);
    }
    else
    {
        // Pupils looking in the direction of movement
        painter.setBrush(Qt::black);
        int pupilR = TILE_SIZE / 12;
        QPoint pupilOffset(0,0);

        switch(ghost.direction) {
        case Left: pupilOffset.setX(-eyeR/2); break;
        case Right: pupilOffset.setX(eyeR/2); break;
        case Up: pupilOffset.setY(-eyeR/2); break;
        case Down: pupilOffset.setY(eyeR/2); break;
        default: break;
        }

        painter.drawEllipse(leftEyeCenter + pupilOffset, pupilR, pupilR);
        painter.drawEllipse(rightEyeCenter + pupilOffset, pupilR, pupilR);
    }
}

void GameWidget::precomputePaths()
{
    qDebug() << "Starting path pre-computation...";
    m_pointToId.clear();
    m_idToPoint.clear();
    m_nextMoveLookup.clear();

    // 1. Map all valid (non-wall) points to a unique ID
    int currentId = 0;
    for (int r = 0; r < MAZE_HEIGHT; ++r) {
        for (int c = 0; c < MAZE_WIDTH; ++c) {
            // We can pathfind *from* any non-wall tile
            if (mazeGrid[r][c] != 1) {
                QPoint p(c, r);
                m_pointToId[p] = currentId;
                m_idToPoint.append(p);
                currentId++;
            }
        }
    }

    int numValidNodes = m_idToPoint.size();
    if (numValidNodes == 0) return;

    // 2. Initialize the DP lookup table
    // m_nextMoveLookup[startId][targetId] = nextMovePoint
    m_nextMoveLookup.resize(numValidNodes);
    for (int i = 0; i < numValidNodes; ++i) {
        m_nextMoveLookup[i].resize(numValidNodes);
    }

    // 3. Run a BFS from *every* valid node to all other nodes
    for (int i = 0; i < numValidNodes; ++i) {
        QPoint startPoint = m_idToPoint[i];
        int startId = i;

        QPoint cameFrom[MAZE_HEIGHT][MAZE_WIDTH];
        bool visited[MAZE_HEIGHT][MAZE_WIDTH] = {false};
        QQueue<QPoint> queue;

        queue.enqueue(startPoint);
        visited[startPoint.y()][startPoint.x()] = true;
        cameFrom[startPoint.y()][startPoint.x()] = startPoint; // Self-loop
        m_nextMoveLookup[startId][startId] = startPoint;       // Move to self is "stop"

        while (!queue.isEmpty()) {
            QPoint current = queue.dequeue();

            QPoint neighbors[4] = {
                QPoint(current.x(), current.y() - 1), // Up
                QPoint(current.x(), current.y() + 1), // Down
                QPoint(current.x() - 1, current.y()), // Left
                QPoint(current.x() + 1, current.y())  // Right
            };

            for (const QPoint &neighbor : neighbors) {
                int nRow = neighbor.y();
                int nCol = neighbor.x();

                // Check bounds, walls, and visited
                if (nRow < 0 || nRow >= MAZE_HEIGHT || nCol < 0 || nCol >= MAZE_WIDTH ||
                    mazeGrid[nRow][nCol] == 1 || visited[nRow][nCol]) {
                    continue;
                }

                // Valid new tile found
                visited[nRow][nCol] = true;
                cameFrom[nRow][nCol] = current;
                queue.enqueue(neighbor);

                // --- This is the DP magic ---
                // We just found the shortest path from startPoint to 'neighbor'
                // Now, trace back to find the *first step* from startPoint

                QPoint firstStep = neighbor;
                QPoint traceBack = current;

                // Keep tracing back until the node *before* us is the start point
                while (traceBack != startPoint) {
                    firstStep = traceBack;
                    traceBack = cameFrom[traceBack.y()][traceBack.x()];
                }

                // We found it! 'firstStep' is the move to make from 'startPoint'.
                int targetId = m_pointToId[neighbor];
                m_nextMoveLookup[startId][targetId] = firstStep;
            }
        }
    }
    qDebug() << "Path pre-computation complete. " << numValidNodes << "nodes processed.";
}


// === GAME STATE MANAGEMENT ===

void GameWidget::resetGame()
{
    m_score = 0;
    m_round = 1; // Reset round to 1
    m_gameState = Menu;
    m_bgMusicPlayer->stop();
}

void GameWidget::startGame()
{
    if (startMacroCol == -1 || startMacroRow == -1) {
        qDebug() << "Start position 'p' not found!";
        return;
    }

    // Don't reset score when coming from Win state (continuing to next round)
    // Only reset score when starting fresh from Menu or retrying from GameOver
    if (m_gameState == Menu || m_gameState == GameOver) {
        m_score = 0;
    }

    // Copy the original maze back into the working maze
    for (int row = 0; row < MAZE_HEIGHT; ++row) {
        for (int col = 0; col < MAZE_WIDTH; ++col) {
            mazeGrid[row][col] = originalMazeGrid[row][col];
        }
    }

    // Set Pac-Man's start position
    QPoint gridCenter = macroGridToGridCenter(startMacroCol, startMacroRow);
    pacman_grid_center = gridCenter;
    pacman_macrogrid_center = QPoint(startMacroCol, startMacroRow);
    m_pacmanDirection = Right;

    // Eat the pellet at the start
    mazeGrid[startMacroRow][startMacroCol] = 3;

    // Initialize ghosts (this will now use m_round)
    initializeGhosts();

    m_gameState = Playing;
    m_bgMusicPlayer->play();
}

// ##################################################################
// ##################################################################
// ###                                                            ###
// ###     ALL YOUR PORTED CORE GAME LOGIC GOES BELOW THIS LINE   ###
// ###   (These are from your mainwindow.cpp, adapted for TILE_SIZE) ###
// ###                                                            ###
// ##################################################################
// ##################################################################

// === ADAPTED from your logic ===
QPoint GameWidget::macroGridToGridCenter(int macroCol, int macroRow)
{
    // Returns the PIXEL center of a macro grid cell
    int gridX = macroCol * TILE_SIZE + (TILE_SIZE / 2);
    int gridY = macroRow * TILE_SIZE + (TILE_SIZE / 2);
    return QPoint(gridX, gridY);
}

// === ADAPTED from your logic ===
QPoint GameWidget::gridToMacroGrid(int gridX, int gridY)
{
    // Returns the macro grid cell (col, row) for a given PIXEL coordinate
    int macroCol = gridX / TILE_SIZE;
    int macroRow = gridY / TILE_SIZE;
    macroCol = qBound(0, macroCol, MAZE_WIDTH - 1);
    macroRow = qBound(0, macroRow, MAZE_HEIGHT - 1);
    return QPoint(macroCol, macroRow);
}

// === ADAPTED from your logic ===
void GameWidget::startAnimatedMove(int tx, int ty) // tx, ty are 0 or +/- TILE_SIZE
{
    if (isMoving) return;

    targetX = tx;
    targetY = ty;
    currentStep = 0;
    isMoving = true;

    // Calculate pixel delta per step
    stepDeltaX = (tx != 0) ? (tx / moveSteps) : 0; // e.g., 32 / 8 = 4 pixels
    stepDeltaY = (ty != 0) ? (ty / moveSteps) : 0; // e.g., 32 / 8 = 4 pixels
}

// === ADAPTED from your logic ===
bool GameWidget::canMove(int tx, int ty) // tx, ty are -1, 0, or 1 (macro grid delta)
{
    int newMacroCol = pacman_macrogrid_center.x() + tx;
    int newMacroRow = pacman_macrogrid_center.y() + ty;

    // Check bounds
    if (newMacroCol < 0 || newMacroCol >= MAZE_WIDTH || newMacroRow < 0 || newMacroRow >= MAZE_HEIGHT) {
        return false; // Can't move off screen
    }

    // Check for wall
    return mazeGrid[newMacroRow][newMacroCol] != 1;
}

// === ADAPTED from your logic ===
void GameWidget::animationStep() // Pac-Man's movement
{
    if (!isMoving) {
        return;
    }
    currentStep++;

    // Move Pac-Man's pixel center
    pacman_grid_center.setX(pacman_grid_center.x() + stepDeltaX);
    pacman_grid_center.setY(pacman_grid_center.y() + stepDeltaY);

    if (currentStep >= moveSteps) {
        // Finished moving
        isMoving = false;

        // Snap to the new grid cell's center
        pacman_macrogrid_center = gridToMacroGrid(pacman_grid_center.x(), pacman_grid_center.y());
        pacman_grid_center = macroGridToGridCenter(pacman_macrogrid_center.x(), pacman_macrogrid_center.y());

        // Check for pellet
        collectPellet();
    }
}

// === PORTED from your logic (Unchanged) ===
void GameWidget::loadMaze(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Could not open file:" << filename;
        return;
    }

    QTextStream in(&file);
    int row = 0;
    startMacroRow = -1;
    startMacroCol = -1;
    ghostSpawnPositions.clear();

    while (!in.atEnd() && row < MAZE_HEIGHT) {
        QString line = in.readLine();
        for (int col = 0; col < MAZE_WIDTH && col < line.length(); col++) {
            QChar ch = line[col];
            int value;
            if (ch == '1') value = 1;
            else if (ch == '3' || ch == 'g') {
                value = 3;
                if (ch == 'g') ghostSpawnPositions.append(QPoint(col, row));
            }
            else if (ch == '4') value = 4;
            else if (ch == '2') value = 2;
            else {
                value = 0;
                if (ch == 'p') {
                    startMacroCol = col;
                    startMacroRow = row;
                }
            }
            mazeGrid[row][col] = value;
            originalMazeGrid[row][col] = value;
        }
        row++;
    }
    file.close();
    precomputePaths();
}

// === PORTED from your logic (Unchanged) ===
void GameWidget::collectPellet()
{
    int macroCol = pacman_macrogrid_center.x();
    int macroRow = pacman_macrogrid_center.y();

    if (macroCol < 0 || macroCol >= MAZE_WIDTH || macroRow < 0 || macroRow >= MAZE_HEIGHT) {
        return;
    }
    int cellValue = mazeGrid[macroRow][macroCol];

    if (cellValue == 0) {
        m_score += 1;
        mazeGrid[macroRow][macroCol] = 3; // Set to empty path
        m_pelletSfx->play();
    } else if (cellValue == 4) {
        m_score += 5;
        mazeGrid[macroRow][macroCol] = 3;
        activatePanicMode();
        m_powerPelletSfx->play();
    }

    if (checkAllPelletsCollected()) {
        resetLevel();
    }
}

// === PORTED from your logic (Unchanged) ===
bool GameWidget::checkAllPelletsCollected()
{
    for (int macroRow = 0; macroRow < MAZE_HEIGHT; macroRow++) {
        for (int macroCol = 0; macroCol < MAZE_WIDTH; macroCol++) {
            int cellValue = mazeGrid[macroRow][macroCol];
            if (cellValue == 0 || cellValue == 4) {
                return false;
            }
        }
    }
    return true;
}


// === PORTED from your logic (Unchanged) ===
void GameWidget::resetLevel()
{
    // Round is cleared! Transition to Win state
    m_gameState = Win;
    m_bgMusicPlayer->stop();
}


// === PORTED from your logic (Unchanged) ===
void GameWidget::activatePanicMode()
{
    for (Ghost &ghost : ghosts) {
        if (ghost.active) {
            ghost.mode = Panic;
            ghost.path.clear();
            ghost.pathIndex = 0;
            // Color is handled by drawGhost
        }
    }
    if (panicTimer->isActive()) {
        panicTimer->stop();
    }
    panicTimer->start(10000); // 10 seconds
}

// === PORTED from your logic (Unchanged) ===
void GameWidget::panicModeTimeout()
{
    for (Ghost &ghost : ghosts) {
        if (ghost.active) {
            ghost.mode = Chase;
            ghost.path.clear();
            ghost.pathIndex = 0;
            // Color is handled by drawGhost
        }
    }
}

// === ADAPTED from your logic (Ghosts) ===
void GameWidget::ghostAnimationStep()
{
    if (m_gameState != Playing) return;

    for (int i = 0; i < ghosts.size(); i++) {
        Ghost &ghost = ghosts[i];
        if (!ghost.active || ghost.respawning) continue;

        if (ghost.speedMultiplier < 1.0f) {
            ghost.delayCounter++;
            int requiredDelay = static_cast<int>((1.0f / ghost.speedMultiplier) - 1.0f);
            if (ghost.delayCounter < requiredDelay) {
                continue;
            }
            ghost.delayCounter = 0;
        }

        if (!ghost.moving) {
            if (ghost.type == IntersectionRandom && isAtIntersection(ghost)) {
                if (QRandomGenerator::global()->bounded(100) < REPRODUCTION_PROB) {
                    spawnChildGhost(ghost);
                }
            }
            moveGhost(ghost);
            continue;
        }

        ghost.currentStep++;
        ghost.grid_center.setX(ghost.grid_center.x() + ghost.stepDeltaX);
        ghost.grid_center.setY(ghost.grid_center.y() + ghost.stepDeltaY);

        if (ghost.currentStep >= ghost.moveSteps) {
            ghost.moving = false;
            ghost.macrogrid_center = gridToMacroGrid(ghost.grid_center.x(), ghost.grid_center.y());
            ghost.grid_center = macroGridToGridCenter(ghost.macrogrid_center.x(), ghost.macrogrid_center.y());
        }
    }
}

// === ADAPTED from your logic (Ghosts) ===
void GameWidget::checkGhostCollisions()
{
    for (Ghost &ghost : ghosts) {
        if (!ghost.active || ghost.respawning) continue;

        // Pixel-based collision check
        int dist = std::abs(pacman_grid_center.x() - ghost.grid_center.x()) +
                   std::abs(pacman_grid_center.y() - ghost.grid_center.y());

        if (dist < TILE_SIZE / 1.5) { // If centers are close
            if (ghost.mode == Chase) {
                qDebug() << "Ghost caught Pacman! Game Over!";
                m_gameState = GameOver;
                m_bgMusicPlayer->stop();
                m_gameOverSfx->play();
                return;
            } else { // Panic mode
                qDebug() << "Pacman ate ghost!";
                m_score += 50;
                ghost.active = false;
                ghost.respawning = true;

                // Use a lambda to capture the specific ghost
                QTimer::singleShot(2000, this, [this, ghostId = ghost.parentId]() {
                    // Find the ghost by its ID to respawn
                    for(Ghost &g : ghosts) {
                        if(g.parentId == ghostId) {
                            respawnGhost(g);
                            break;
                        }
                    }
                });
            }
        }
    }
}

// === PORTED from your logic (Unchanged) ===
Color GameWidget::getGhostColor(const Ghost &ghost)
{
    // This is your exact function, just without the panic part
    switch (ghost.type) {
    case Original:
        return {255, 0, 0}; // Red
    case AggressiveChaser:
        return {255, 165, 0}; // Orange
    case Ambusher:
        return {255, 105, 180}; // Pink
    case RandomPatrol:
        return {0, 255, 255}; // Cyan
    case IntersectionRandom:
        return {255, 255, 0}; // Yellow
    default:
        return {255, 0, 0};
    }
}

// === PORTED from your logic (Unchanged) ===
void GameWidget::initializeGhosts()
{
    ghosts.clear();
    nextGhostId = 0;

    if (ghostSpawnPositions.isEmpty()) return;

    int numGhosts = 0;
    if (m_round == 1) numGhosts = 0;
    if (m_round == 2) numGhosts = 1;
    if (m_round == 3) numGhosts = 2;
    if (m_round == 4) numGhosts = 3;
    if (m_round == 5) numGhosts = 4;
    if (m_round == 6) numGhosts = 4;
    if (m_round == 7) numGhosts = 1;

    for (int i = 0; i < numGhosts && i < ghostSpawnPositions.size(); i++) {
        Ghost ghost;
        GhostType type;
        if (m_round == 7) type = IntersectionRandom;
        else if (i == 0) type = Original;
        else if (i == 1) type = IntersectionRandom;
        else if (i == 2) type = Ambusher;
        else type = RandomPatrol;
        initializeGhost(ghost, i, type);
        if (m_round == 6) {
            if (i == 0) ghost.speedMultiplier = 0.5f;
            else if (i == 1) ghost.speedMultiplier = 0.7f;
            else if (i == 2) ghost.speedMultiplier = 0.9f;
            else if (i == 3) ghost.speedMultiplier = 1.1f;
        }
        if (m_round == 7) {
            ghost.speedMultiplier = 0.5f;
        }
        ghosts.append(ghost);
    }
}

// === ADAPTED from your logic (Ghosts) ===
void GameWidget::initializeGhost(Ghost &ghost, int spawnIndex, GhostType type)
{
    QPoint spawnMacro = ghostSpawnPositions[spawnIndex % ghostSpawnPositions.size()];
    QPoint spawnGrid = macroGridToGridCenter(spawnMacro.x(), spawnMacro.y());

    ghost.grid_center = spawnGrid;
    ghost.macrogrid_center = spawnMacro;
    ghost.type = type;
    ghost.active = true;
    ghost.mode = Chase;
    ghost.direction = Stop;
    ghost.moving = false;
    ghost.moveSteps = MOVESTEPS; // Adapted
    ghost.currentStep = 0;
    ghost.path.clear();
    ghost.pathIndex = 0;
    ghost.respawning = false;
    ghost.failCounter = 0;
    ghost.speedMultiplier = 1.0f;
    ghost.moveDelay = 0;
    ghost.delayCounter = 0;
    ghost.parentId = nextGhostId++;
    ghost.color = getGhostColor(ghost); // Set its color
}

// === ADAPTED from your logic (Ghosts) ===
void GameWidget::respawnGhost(Ghost &ghost)
{
    if (ghostSpawnPositions.isEmpty()) {
        ghost.respawning = false;
        return;
    }
    QPoint spawnMacro = ghostSpawnPositions[0];
    QPoint spawnGrid = macroGridToGridCenter(spawnMacro.x(), spawnMacro.y());

    ghost.grid_center = spawnGrid;
    ghost.macrogrid_center = spawnMacro;
    ghost.active = true;
    ghost.respawning = false;
    ghost.mode = Chase;
    ghost.direction = Stop;
    ghost.path.clear();
    ghost.pathIndex = 0;
    ghost.moving = false;
    ghost.currentStep = 0;
    // Color is set automatically by drawGhost
}

// === ADAPTED from your logic (Ghosts) ===
void GameWidget::spawnChildGhost(const Ghost &parent)
{
    if (ghostSpawnPositions.isEmpty() || ghosts.size() >= MAX_GHOSTS) return;
    Ghost child;
    child.grid_center = parent.grid_center;
    child.macrogrid_center = parent.macrogrid_center;
    child.type = IntersectionRandom;
    child.active = true;
    child.mode = Chase;
    child.direction = Stop;
    child.moving = false;
    child.moveSteps = MOVESTEPS; // Adapted
    child.currentStep = 0;
    child.path.clear();
    child.pathIndex = 0;
    child.respawning = false;
    child.failCounter = 0;
    child.speedMultiplier = 0.5f;
    child.moveDelay = 0;
    child.delayCounter = 0;
    child.parentId = nextGhostId++;
    child.color = getGhostColor(child);
    ghosts.append(child);
}

// === ADAPTED from your logic (Ghosts) ===
void GameWidget::moveGhost(Ghost &ghost)
{
    if (!ghost.active || ghost.moving) return;

    ghost.direction = getGhostDirection(ghost);

    if (ghost.direction == Stop) {
        ghost.path.clear();
        ghost.pathIndex = 0;
        return;
    }

    ghost.moving = true;
    ghost.currentStep = 0;
    ghost.moveSteps = MOVESTEPS; // Adapted

    int stepSize = TILE_SIZE / ghost.moveSteps; // e.g., 4 pixels
    switch (ghost.direction) {
    case Up:    ghost.stepDeltaX = 0; ghost.stepDeltaY = -stepSize; break;
    case Down:  ghost.stepDeltaX = 0; ghost.stepDeltaY = stepSize;  break;
    case Left:  ghost.stepDeltaX = -stepSize; ghost.stepDeltaY = 0; break;
    case Right: ghost.stepDeltaX = stepSize;  ghost.stepDeltaY = 0; break;
    default:    ghost.moving = false; return;
    }
}

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getGhostDirection(Ghost &ghost)
{
    if (ghost.mode == Panic) {
        return getGhostPanicDirection(ghost);
    }
    switch (ghost.type) {
    case Original: return getGhostChaseDirection(ghost);
    case AggressiveChaser: return getAggressiveChaserDirection(ghost);
    case Ambusher: return getAmbusherDirection(ghost);
    case RandomPatrol: return getRandomPatrolDirection(ghost);
    case IntersectionRandom: return getIntersectionRandomDirection(ghost);
    default: return getGhostChaseDirection(ghost);
    }
}

// === PORTED from your logic (Unchanged) ===
// QVector<QPoint> GameWidget::getNeighborMacroCells(QPoint macroCell)
// {
//     QVector<QPoint> neighbors;
//     int col = macroCell.x();
//     int row = macroCell.y();
//     if (row > 0 && mazeGrid[row - 1][col] != 1) neighbors.append(QPoint(col, row - 1));
//     if (row < MAZE_HEIGHT - 1 && mazeGrid[row + 1][col] != 1) neighbors.append(QPoint(col, row + 1));
//     if (col > 0 && mazeGrid[row][col - 1] != 1) neighbors.append(QPoint(col - 1, row));
//     if (col < MAZE_WIDTH - 1 && mazeGrid[row][col + 1] != 1) neighbors.append(QPoint(col + 1, row));
//     return neighbors;
// }

// // === PORTED from your logic (Unchanged) ===
// QVector<QPoint> GameWidget::findPath(QPoint start, QPoint target)
// {
//     QQueue<QPoint> queue;
//     QHash<QPoint, QPoint> cameFrom;
//     QSet<QPoint> visited;
//     queue.enqueue(start);
//     visited.insert(start);
//     cameFrom[start] = start;
//     bool foundTarget = false;
//     while (!queue.isEmpty()) {
//         QPoint current = queue.dequeue();
//         if (current == target) {
//             foundTarget = true;
//             break;
//         }
//         QVector<QPoint> neighbors = getNeighborMacroCells(current);
//         for (const QPoint &neighbor : neighbors) {
//             if (!visited.contains(neighbor)) {
//                 visited.insert(neighbor);
//                 cameFrom[neighbor] = current;
//                 queue.enqueue(neighbor);
//             }
//         }
//     }
//     QVector<QPoint> path;
//     if (foundTarget) {
//         QPoint current = target;
//         while (current != start) {
//             path.prepend(current);
//             current = cameFrom[current];
//         }
//     }
//     return path;
// }

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getGhostChaseDirection(Ghost &ghost)
{
    QPoint ghostMacro = ghost.macrogrid_center;
    QPoint pacmanMacro = pacman_macrogrid_center;

    // Check if points are valid (in case one is in a wall, though they shouldn't be)
    if (!m_pointToId.contains(ghostMacro) || !m_pointToId.contains(pacmanMacro)) {
        return Stop;
    }

    // --- DP TABLE LOOKUP ---
    int startId = m_pointToId[ghostMacro];
    int targetId = m_pointToId[pacmanMacro];
    QPoint nextMove = m_nextMoveLookup[startId][targetId];

    // --- Convert the 'nextMove' point to a Direction ---
    int dx = nextMove.x() - ghostMacro.x();
    int dy = nextMove.y() - ghostMacro.y();

    if (dx > 0) return Right;
    if (dx < 0) return Left;
    if (dy > 0) return Down;
    if (dy < 0) return Up;

    return Stop; // This happens if ghost is already at the target
}

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getAggressiveChaserDirection(Ghost &ghost)
{
    ghost.failCounter++;
    if (ghost.failCounter >= 8) {
        ghost.failCounter = 0;
        QVector<Direction> validDirs;
        if (canGhostMove(ghost, Up)) validDirs.append(Up);
        if (canGhostMove(ghost, Down)) validDirs.append(Down);
        if (canGhostMove(ghost, Left)) validDirs.append(Left);
        if (canGhostMove(ghost, Right)) validDirs.append(Right);
        if (!validDirs.isEmpty()) {
            return validDirs[QRandomGenerator::global()->bounded(validDirs.size())];
        }
    }
    return getGhostChaseDirection(ghost);
}

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getAmbusherDirection(Ghost &ghost)
{
    QPoint ghostMacro = ghost.macrogrid_center;
    QPoint pacmanMacro = pacman_macrogrid_center;
    QPoint targetMacro = pacmanMacro;

    // Target 4 tiles ahead of Pac-Man
    switch(m_pacmanDirection) {
    case Up:    targetMacro.setY(targetMacro.y() - 4); break;
    case Down:  targetMacro.setY(targetMacro.y() + 4); break;
    case Left:  targetMacro.setX(targetMacro.x() - 4); break;
    case Right: targetMacro.setX(targetMacro.x() + 4); break;
    default: break;
    }

    // Clamp to maze bounds
    targetMacro.setX(qBound(0, targetMacro.x(), MAZE_WIDTH - 1));
    targetMacro.setY(qBound(0, targetMacro.y(), MAZE_HEIGHT - 1));

    // If target is a wall or invalid, default to chasing Pac-Man directly
    if (mazeGrid[targetMacro.y()][targetMacro.x()] == 1 || !m_pointToId.contains(targetMacro)) {
        targetMacro = pacmanMacro;
    }

    // --- DP TABLE LOOKUP ---
    if (!m_pointToId.contains(ghostMacro)) return Stop; // Should not happen

    int startId = m_pointToId[ghostMacro];
    int targetId = m_pointToId[targetMacro];
    QPoint nextMove = m_nextMoveLookup[startId][targetId];

    // --- Convert the 'nextMove' point to a Direction ---
    int dx = nextMove.x() - ghostMacro.x();
    int dy = nextMove.y() - ghostMacro.y();

    if (dx > 0) return Right;
    if (dx < 0) return Left;
    if (dy > 0) return Down;
    if (dy < 0) return Up;

    return Stop;
}

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getRandomPatrolDirection(Ghost &ghost)
{
    if (ghost.direction != Stop && QRandomGenerator::global()->bounded(100) < 70) {
        if (canGhostMove(ghost, ghost.direction)) {
            return ghost.direction;
        }
    }
    QVector<Direction> validDirs;
    if (canGhostMove(ghost, Up)) validDirs.append(Up);
    if (canGhostMove(ghost, Down)) validDirs.append(Down);
    if (canGhostMove(ghost, Left)) validDirs.append(Left);
    if (canGhostMove(ghost, Right)) validDirs.append(Right);
    if (!validDirs.isEmpty()) {
        return validDirs[QRandomGenerator::global()->bounded(validDirs.size())];
    }
    return Stop;
}

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getIntersectionRandomDirection(Ghost &ghost)
{
    if (isAtIntersection(ghost)) {
        QVector<Direction> validDirs;
        if (canGhostMove(ghost, Up)) validDirs.append(Up);
        if (canGhostMove(ghost, Down)) validDirs.append(Down);
        if (canGhostMove(ghost, Left)) validDirs.append(Left);
        if (canGhostMove(ghost, Right)) validDirs.append(Right);
        if (!validDirs.isEmpty()) {
            return validDirs[QRandomGenerator::global()->bounded(validDirs.size())];
        }
    } else {
        if (ghost.direction != Stop && canGhostMove(ghost, ghost.direction)) {
            return ghost.direction;
        }
        QVector<Direction> validDirs;
        if (canGhostMove(ghost, Up)) validDirs.append(Up);
        if (canGhostMove(ghost, Down)) validDirs.append(Down);
        if (canGhostMove(ghost, Left)) validDirs.append(Left);
        if (canGhostMove(ghost, Right)) validDirs.append(Right);
        if (!validDirs.isEmpty()) {
            return validDirs[QRandomGenerator::global()->bounded(validDirs.size())];
        }
    }
    return Stop;
}

// === PORTED from your logic (Unchanged) ===
Direction GameWidget::getGhostPanicDirection(Ghost &ghost)
{
    // Run to a random valid neighbor
    if (isAtIntersection(ghost) && QRandomGenerator::global()->bounded(100) < 50) {
        QVector<Direction> validDirs;
        if (canGhostMove(ghost, Up)) validDirs.append(Up);
        if (canGhostMove(ghost, Down)) validDirs.append(Down);
        if (canGhostMove(ghost, Left)) validDirs.append(Left);
        if (canGhostMove(ghost, Right)) validDirs.append(Right);
        if (!validDirs.isEmpty()) {
            return validDirs[QRandomGenerator::global()->bounded(validDirs.size())];
        }
    }

    // Try to run away from Pac-Man
    QPoint ghostMacro = ghost.macrogrid_center;
    QPoint pacmanMacro = pacman_macrogrid_center;
    int dx = ghostMacro.x() - pacmanMacro.x();
    int dy = ghostMacro.y() - pacmanMacro.y();

    if (std::abs(dx) > std::abs(dy)) {
        if (dx > 0 && canGhostMove(ghost, Right)) return Right;
        if (dx < 0 && canGhostMove(ghost, Left)) return Left;
    } else {
        if (dy > 0 && canGhostMove(ghost, Down)) return Down;
        if (dy < 0 && canGhostMove(ghost, Up)) return Up;
    }

    // Fallback to random
    return getRandomPatrolDirection(ghost);
}

// === ADAPTED from your logic (Ghosts) ===
bool GameWidget::canGhostMove(const Ghost &ghost, Direction dir)
{
    int tx = 0, ty = 0;
    switch(dir) {
    case Up: ty = -1; break;
    case Down: ty = 1; break;
    case Left: tx = -1; break;
    case Right: tx = 1; break;
    default: return false;
    }

    int newMacroCol = ghost.macrogrid_center.x() + tx;
    int newMacroRow = ghost.macrogrid_center.y() + ty;

    if (newMacroCol < 0 || newMacroCol >= MAZE_WIDTH || newMacroRow < 0 || newMacroRow >= MAZE_HEIGHT) {
        return false;
    }

    // Ghosts can't enter wall (1) or spawn (2)
    int cell = mazeGrid[newMacroRow][newMacroCol];
    // ===========================
    // return (cell != 1 && cell != 2);
    return (cell != 1);
}

// === PORTED from your logic (Unchanged) ===
bool GameWidget::isAtIntersection(const Ghost &ghost)
{
    QPoint macro = ghost.macrogrid_center;
    int pathCount = 0;
    if (macro.y() > 0 && mazeGrid[macro.y() - 1][macro.x()] != 1) pathCount++;
    if (macro.y() < MAZE_HEIGHT - 1 && mazeGrid[macro.y() + 1][macro.x()] != 1) pathCount++;
    if (macro.x() > 0 && mazeGrid[macro.y()][macro.x() - 1] != 1) pathCount++;
    if (macro.x() < MAZE_WIDTH - 1 && mazeGrid[macro.y()][macro.x() + 1] != 1) pathCount++;
    return pathCount >= 3;
}
