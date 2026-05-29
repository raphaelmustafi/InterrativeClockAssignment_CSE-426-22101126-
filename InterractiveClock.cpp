#include <GL/glut.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

const int WINDOW_WIDTH = 720;
const int WINDOW_HEIGHT = 720;
const int CENTER_X = WINDOW_WIDTH / 2;
const int CENTER_Y = WINDOW_HEIGHT / 2;
const double PI = 3.14159265358979323846;
const double TWO_PI = 2.0 * PI;

struct Color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

struct Theme {
    Color background;
    Color ring;
    Color ringSoft;
    Color hourTick;
    Color minuteTick;
    Color hourHand;
    Color minuteHand;
    Color secondHand;
    Color clip;
    Color hub;
};

struct Rect {
    double left;
    double right;
    double top;
    double bottom;
};

Theme themes[] = {
    {{18, 23, 20}, {67, 214, 181}, {95, 128, 116}, {255, 189, 74}, {145, 161, 145},
     {243, 240, 232}, {67, 214, 181}, {231, 96, 109}, {255, 189, 74}, {255, 248, 221}},
    {{32, 28, 20}, {255, 189, 74}, {130, 100, 54}, {66, 214, 181}, {170, 153, 123},
     {255, 246, 218}, {255, 189, 74}, {240, 106, 138}, {143, 199, 255}, {255, 246, 218}},
    {{18, 18, 18}, {230, 230, 224}, {110, 110, 105}, {255, 189, 74}, {150, 150, 144},
     {238, 238, 232}, {190, 190, 184}, {231, 96, 109}, {67, 214, 181}, {255, 255, 248}}
};

enum ClipMode {
    CLIP_NONE,
    CLIP_CIRCLE,
    CLIP_BOX
};

bool paused = false;
int themeIndex = 0;
int radiusValue = 270;
int tickLength = 25;
int hourThickness = 11;
int minuteThickness = 7;
int secondThickness = 3;
int speedMultiplier = 1;
ClipMode clipMode = CLIP_CIRCLE;
double simulatedSeconds = 0.0;
int previousElapsedMs = 0;

void setColor(Color color) {
    glColor3ub(color.r, color.g, color.b);
}

bool insideCircleClip(int x, int y) {
    int clipRadius = radiusValue + 14;
    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;
    return dx * dx + dy * dy <= clipRadius * clipRadius;
}

void plotPixel(int x, int y, Color color) {
    if (x < 0 || x >= WINDOW_WIDTH || y < 0 || y >= WINDOW_HEIGHT) {
        return;
    }

    if (clipMode == CLIP_CIRCLE && !insideCircleClip(x, y)) {
        return;
    }

    setColor(color);
    glVertex2i(x, y);
}

void drawCircleSymmetry(int cx, int cy, int x, int y, Color color) {
    plotPixel(cx + x, cy + y, color);
    plotPixel(cx - x, cy + y, color);
    plotPixel(cx + x, cy - y, color);
    plotPixel(cx - x, cy - y, color);
    plotPixel(cx + y, cy + x, color);
    plotPixel(cx - y, cy + x, color);
    plotPixel(cx + y, cy - x, color);
    plotPixel(cx - y, cy - x, color);
}

void drawMidpointCircle(int cx, int cy, int radius, Color color, int thickness) {
    int half = thickness / 2;

    for (int offset = -half; offset <= half; offset++) {
        int r = radius + offset;
        if (r < 0) {
            continue;
        }

        int x = 0;
        int y = r;
        int decision = 1 - r;
        drawCircleSymmetry(cx, cy, x, y, color);

        while (x < y) {
            x++;
            if (decision < 0) {
                decision += 2 * x + 1;
            }
            else {
                y--;
                decision += 2 * (x - y) + 1;
            }
            drawCircleSymmetry(cx, cy, x, y, color);
        }
    }
}

void fillDisk(int cx, int cy, int radius, Color color) {
    int radiusSquared = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radiusSquared) {
                plotPixel(cx + x, cy + y, color);
            }
        }
    }
}

void drawBresenhamLine(int x0, int y0, int x1, int y1, Color color, int thickness) {
    int dx = std::abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int error = dx + dy;
    int brushRadius = std::max(0, (thickness + 1) / 2 - 1);

    while (true) {
        if (brushRadius == 0) {
            plotPixel(x0, y0, color);
        }
        else {
            fillDisk(x0, y0, brushRadius, color);
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

const int REGION_INSIDE = 0;
const int REGION_LEFT = 1;
const int REGION_RIGHT = 2;
const int REGION_BOTTOM = 4;
const int REGION_TOP = 8;

int regionCode(double x, double y, Rect rect) {
    int code = REGION_INSIDE;
    if (x < rect.left) {
        code |= REGION_LEFT;
    }
    else if (x > rect.right) {
        code |= REGION_RIGHT;
    }
    if (y < rect.top) {
        code |= REGION_TOP;
    }
    else if (y > rect.bottom) {
        code |= REGION_BOTTOM;
    }
    return code;
}

bool cohenSutherlandClip(double& x0, double& y0, double& x1, double& y1, Rect rect) {
    int code0 = regionCode(x0, y0, rect);
    int code1 = regionCode(x1, y1, rect);

    while (true) {
        if ((code0 | code1) == 0) {
            return true;
        }
        if ((code0 & code1) != 0) {
            return false;
        }

        int outCode = (code0 != 0) ? code0 : code1;
        double x = 0.0;
        double y = 0.0;

        if (outCode & REGION_TOP) {
            x = x0 + (x1 - x0) * (rect.top - y0) / (y1 - y0);
            y = rect.top;
        }
        else if (outCode & REGION_BOTTOM) {
            x = x0 + (x1 - x0) * (rect.bottom - y0) / (y1 - y0);
            y = rect.bottom;
        }
        else if (outCode & REGION_RIGHT) {
            y = y0 + (y1 - y0) * (rect.right - x0) / (x1 - x0);
            x = rect.right;
        }
        else if (outCode & REGION_LEFT) {
            y = y0 + (y1 - y0) * (rect.left - x0) / (x1 - x0);
            x = rect.left;
        }

        if (outCode == code0) {
            x0 = x;
            y0 = y;
            code0 = regionCode(x0, y0, rect);
        }
        else {
            x1 = x;
            y1 = y;
            code1 = regionCode(x1, y1, rect);
        }
    }
}

Rect currentClipBox() {
    int marginX = radiusValue / 3;
    int marginY = radiusValue / 7;
    return {
        static_cast<double>(CENTER_X - radiusValue + marginX),
        static_cast<double>(CENTER_X + radiusValue - marginX),
        static_cast<double>(CENTER_Y - radiusValue + marginY),
        static_cast<double>(CENTER_Y + radiusValue - marginY)
    };
}

void drawLineMaybeClipped(double x0, double y0, double x1, double y1, Color color, int thickness) {
    if (clipMode == CLIP_BOX) {
        Rect rect = currentClipBox();
        if (!cohenSutherlandClip(x0, y0, x1, y1, rect)) {
            return;
        }
    }

    drawBresenhamLine(
        static_cast<int>(std::round(x0)),
        static_cast<int>(std::round(y0)),
        static_cast<int>(std::round(x1)),
        static_cast<int>(std::round(y1)),
        color,
        thickness
    );
}

void pointOnRadius(double radius, double angle, double& x, double& y) {
    x = CENTER_X + std::cos(angle) * radius;
    y = CENTER_Y + std::sin(angle) * radius;
}

void drawClockFace(Theme theme) {
    drawMidpointCircle(CENTER_X, CENTER_Y, radiusValue + 17, theme.ringSoft, 2);
    drawMidpointCircle(CENTER_X, CENTER_Y, radiusValue + 10, theme.ring, 3);
    drawMidpointCircle(CENTER_X, CENTER_Y, radiusValue, theme.ring, 2);
    drawMidpointCircle(CENTER_X, CENTER_Y, radiusValue - 12, theme.ringSoft, 1);

    for (int i = 0; i < 60; i++) {
        double angle = (static_cast<double>(i) / 60.0) * TWO_PI - PI / 2.0;
        bool isHour = (i % 5 == 0);
        double innerRadius = radiusValue - (isHour ? tickLength : tickLength * 0.48);
        double outerRadius = radiusValue - 4;
        double x0, y0, x1, y1;

        pointOnRadius(innerRadius, angle, x0, y0);
        pointOnRadius(outerRadius, angle, x1, y1);
        drawLineMaybeClipped(x0, y0, x1, y1, isHour ? theme.hourTick : theme.minuteTick, isHour ? 4 : 2);
    }
}

void drawClipBox(Color color) {
    Rect r = currentClipBox();
    drawBresenhamLine(r.left, r.top, r.right, r.top, color, 2);
    drawBresenhamLine(r.right, r.top, r.right, r.bottom, color, 2);
    drawBresenhamLine(r.right, r.bottom, r.left, r.bottom, color, 2);
    drawBresenhamLine(r.left, r.bottom, r.left, r.top, color, 2);
}

void handEndpoint(double handLength, double units, double totalUnits, double& x, double& y) {
    double angle = (units / totalUnits) * TWO_PI - PI / 2.0;
    pointOnRadius(handLength, angle, x, y);
}

void drawHands(Theme theme) {
    double seconds = std::fmod(simulatedSeconds, 60.0);
    double totalMinutes = simulatedSeconds / 60.0;
    double minutes = std::fmod(totalMinutes, 60.0);
    double hours = std::fmod(totalMinutes / 60.0, 12.0);

    double secondX, secondY, minuteX, minuteY, hourX, hourY, tailX, tailY;
    handEndpoint(radiusValue * 0.84, seconds, 60.0, secondX, secondY);
    handEndpoint(radiusValue * 0.70, minutes, 60.0, minuteX, minuteY);
    handEndpoint(radiusValue * 0.48, hours, 12.0, hourX, hourY);
    handEndpoint(radiusValue * 0.16, seconds + 30.0, 60.0, tailX, tailY);

    drawLineMaybeClipped(CENTER_X, CENTER_Y, hourX, hourY, theme.hourHand, hourThickness);
    drawLineMaybeClipped(CENTER_X, CENTER_Y, minuteX, minuteY, theme.minuteHand, minuteThickness);
    drawLineMaybeClipped(tailX, tailY, secondX, secondY, theme.secondHand, secondThickness);
    fillDisk(CENTER_X, CENTER_Y, 10, theme.hub);
    drawMidpointCircle(CENTER_X, CENTER_Y, 13, theme.secondHand, 2);
}

void updateWindowTitle() {
    int totalSeconds = static_cast<int>(std::floor(simulatedSeconds));
    int second = totalSeconds % 60;
    int minute = (totalSeconds / 60) % 60;
    int hour = (totalSeconds / 3600) % 24;

    std::string clipName = "None";
    if (clipMode == CLIP_CIRCLE) {
        clipName = "Circle";
    }
    else if (clipMode == CLIP_BOX) {
        clipName = "Cohen-Sutherland Box";
    }

    std::ostringstream title;
    title << "GLUT Graphics Clock | "
        << std::setfill('0') << std::setw(2) << hour << ":"
        << std::setw(2) << minute << ":"
        << std::setw(2) << second
        << " | Clip: " << clipName
        << " | Speed: " << speedMultiplier << "x";
    glutSetWindowTitle(title.str().c_str());
}

void display() {
    Theme theme = themes[themeIndex];
    glClearColor(theme.background.r / 255.0f, theme.background.g / 255.0f, theme.background.b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBegin(GL_POINTS);
    drawClockFace(theme);
    drawHands(theme);
    if (clipMode == CLIP_BOX) {
        drawClipBox(theme.clip);
    }
    glEnd();

    glutSwapBuffers();
}

void timer(int) {
    int elapsedMs = glutGet(GLUT_ELAPSED_TIME);
    int deltaMs = elapsedMs - previousElapsedMs;
    previousElapsedMs = elapsedMs;

    if (!paused) {
        simulatedSeconds += (deltaMs / 1000.0) * speedMultiplier;
    }

    updateWindowTitle();
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void resetToSystemTime() {
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    simulatedSeconds = localTime->tm_hour * 3600.0 + localTime->tm_min * 60.0 + localTime->tm_sec;
}

void keyboard(unsigned char key, int, int) {
    switch (key) {
    case 27:
        std::exit(0);
        break;
    case 'p':
    case 'P':
        paused = !paused;
        break;
    case 'n':
    case 'N':
        resetToSystemTime();
        paused = false;
        break;
    case '+':
    case '=':
        speedMultiplier = std::min(12, speedMultiplier + 1);
        break;
    case '-':
    case '_':
        speedMultiplier = std::max(1, speedMultiplier - 1);
        break;
    case 't':
    case 'T':
        themeIndex = (themeIndex + 1) % 3; // Case "T or t" to change theme.
        break;
    case 'c':
    case 'C':
        clipMode = static_cast<ClipMode>((clipMode + 1) % 3);
        break;
    case '[':
        radiusValue = std::max(210, radiusValue - 5);
        break;
    case ']':
        radiusValue = std::min(320, radiusValue + 5);
        break;
    case '1':
        hourThickness = (hourThickness >= 15) ? 7 : hourThickness + 2;
        break;
    case '2':
        minuteThickness = (minuteThickness >= 11) ? 5 : minuteThickness + 2;
        break;
    case '3':
        secondThickness = (secondThickness >= 5) ? 1 : secondThickness + 1;
        break;
    default:
        break;
    }
    glutPostRedisplay();
}

void menu(int option) {
    switch (option) {
    case 1:
        paused = !paused;
        break;
    case 2:
        resetToSystemTime();
        paused = false;
        break;
    case 3:
        themeIndex = (themeIndex + 1) % 3; // Change theme.
        break;
    case 4:
        clipMode = CLIP_NONE;
        break;
    case 5:
        clipMode = CLIP_CIRCLE;
        break;
    case 6:
        clipMode = CLIP_BOX;
        break;
    case 7:
        std::exit(0);
        break;
    default:
        break;
    }
    glutPostRedisplay();
}

void initOpenGL() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPointSize(1.0f);
}

void createMenu() {
    glutCreateMenu(menu);
    glutAddMenuEntry("Pause / Resume (P)", 1);
    glutAddMenuEntry("Set Current Time (N)", 2);
    glutAddMenuEntry("Change Theme (T)", 3);
    glutAddMenuEntry("No Clipping", 4);
    glutAddMenuEntry("Circular Clipping", 5);
    glutAddMenuEntry("Cohen-Sutherland Box Clipping", 6);
    glutAddMenuEntry("Exit", 7);
    glutAttachMenu(GLUT_RIGHT_BUTTON);
}

int main(int argc, char** argv) {
    resetToSystemTime();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    glutCreateWindow("GLUT Graphics Clock");

    initOpenGL();
    createMenu();

    previousElapsedMs = glutGet(GLUT_ELAPSED_TIME);
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
