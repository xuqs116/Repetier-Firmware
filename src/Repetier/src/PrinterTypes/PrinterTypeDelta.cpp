/*
    This file is part of Repetier-Firmware.

    Repetier-Firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Repetier-Firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Repetier-Firmware.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "../../Repetier.h"

#if PRINTER_TYPE == 2

float PrinterType::diagonal;
float PrinterType::horizontalRadius;
float PrinterType::printRadius;
float PrinterType::printRadiusSquared;
float PrinterType::angleA, PrinterType::angleB, PrinterType::angleC;
float PrinterType::correctionA, PrinterType::correctionB, PrinterType::correctionC;
float PrinterType::radiusCorrectionA, PrinterType::radiusCorrectionB, PrinterType::radiusCorrectionC;
float PrinterType::diagonalSquaredA;
float PrinterType::diagonalSquaredB;
float PrinterType::diagonalSquaredC;
float PrinterType::APosX, PrinterType::APosY;
float PrinterType::BPosX, PrinterType::BPosY;
float PrinterType::CPosX, PrinterType::CPosY;
float PrinterType::homeOffsetA, PrinterType::homeOffsetB, PrinterType::homeOffsetC;
uint16_t PrinterType::eeprom; // start position eeprom
PrinterType::MotionMode PrinterType::mode = PrinterType::MotionMode::MOTION_DELTA;

void PrinterType::setMotionMode(MotionMode newMode) {
    if (mode == newMode) {
        return;
    }
    Motion1::waitForEndOfMoves();
    mode = newMode;
    Motion2::setMotorPositionFromTransformed();
}
bool PrinterType::isAnyEndstopTriggered(bool& moveX, bool& moveY, bool& moveZ) {
    moveX = Motion1::motors[X_AXIS]->getMaxEndstop()->triggered();
    moveY = Motion1::motors[Y_AXIS]->getMaxEndstop()->triggered();
    moveZ = Motion1::motors[Z_AXIS]->getMaxEndstop()->triggered();
    return (moveX || moveY || moveZ);
}
// Moves axis 10 mm down if it is triggered to get a clean start for probing
bool PrinterType::untriggerEndstops() {
    bool moveX, moveY, moveZ;
    if (!isAnyEndstopTriggered(moveX, moveY, moveZ)) {
        return false;
    }
    Motion1::currentPosition[X_AXIS] = Motion1::maxPos[Z_AXIS];
    Motion1::currentPosition[Y_AXIS] = Motion1::maxPos[Z_AXIS];
    Motion1::currentPosition[Z_AXIS] = Motion1::maxPos[Z_AXIS];
    Motion1::updatePositionsFromCurrent();
    setMotionMode(MotionMode::MOTION_PER_AXIS);
    Motion1::setTmpPositionXYZ(
        moveX ? -10.0f : 0,
        moveY ? -10.0f : 0,
        moveZ ? -10.0f : 0);
    Motion1::moveRelativeByOfficial(Motion1::tmpPosition, Motion1::homingFeedrate[Z_AXIS], false);
    Motion1::waitForEndOfMoves();
    moveX = Motion1::motors[X_AXIS]->getMaxEndstop()->triggered();
    moveY = Motion1::motors[Y_AXIS]->getMaxEndstop()->triggered();
    moveZ = Motion1::motors[Z_AXIS]->getMaxEndstop()->triggered();
    setMotionMode(MotionMode::MOTION_DELTA);
    return isAnyEndstopTriggered(moveX, moveY, moveZ);
}

void PrinterType::homeZ() {
    Motion1::toolOffset[X_AXIS] = Motion1::toolOffset[Y_AXIS] = 0;
    Com::printFLN(PSTR("Untrigger endstops"));
    if (untriggerEndstops()) {
        Com::printErrorF(PSTR("Unable to untrigger endstops - giving up!"));
        return;
    }
    Motion1::currentPosition[X_AXIS] = 0;
    Motion1::currentPosition[Y_AXIS] = 0;
    Motion1::currentPosition[Z_AXIS] = Motion1::maxPos[Z_AXIS];
    Motion1::updatePositionsFromCurrent();
    Motion2::setMotorPositionFromTransformed();
    Com::printFLN(PSTR("simpleHome"));
    Motion1::simpleHome(Z_AXIS);

    // Correct end position
    Motion1::waitForEndOfMoves();
    Com::printFLN(PSTR("Offsets"));
    float oldPosition[NUM_AXES];
    FOR_ALL_AXES(i) {
        oldPosition[i] = Motion1::currentPosition[i];
    }
    setMotionMode(MotionMode::MOTION_PER_AXIS);
    Motion1::updatePositionsFromCurrent();
    Motion2::setMotorPositionFromTransformed();
    Motion1::setTmpPositionXYZ(
        -homeOffsetA,
        -homeOffsetB,
        -homeOffsetC);
    Motion1::moveRelativeByOfficial(Motion1::tmpPosition, Motion1::homingFeedrate[Z_AXIS], false);
    Motion1::waitForEndOfMoves();
    setMotionMode(MotionMode::MOTION_DELTA);
    FOR_ALL_AXES(i) {
        Motion1::currentPosition[i] = oldPosition[i];
    }
    Motion1::updatePositionsFromCurrent();
    Motion2::setMotorPositionFromTransformed();
    Motion1::setAxisHomed(X_AXIS, true);
    Motion1::setAxisHomed(Y_AXIS, true);
    Motion1::setAxisHomed(Z_AXIS, true);
}

void PrinterType::homeAxis(fast8_t axis) {
    if (axis < Z_AXIS) {
        return; // Deltals can not home x or y
    } else if (axis > Z_AXIS) {
        Motion1::simpleHome(axis); // Non delta axis, default homing
    } else {                       // XYZ homing
        homeZ();
    }
}

bool PrinterType::positionAllowed(float pos[NUM_AXES]) {
    if (Printer::isNoDestinationCheck()) {
        return true;
    }
    if (Printer::isHoming()) {
        return true;
    }
    if (pos[Z_AXIS] < Motion1::minPos[Z_AXIS] || pos[Z_AXIS] > Motion1::maxPos[Z_AXIS]) {
        return false;
    }
    return pos[X_AXIS] * pos[X_AXIS] + pos[Y_AXIS] * pos[Y_AXIS] <= printRadiusSquared;
}

void PrinterType::transform(float pos[NUM_AXES], int32_t motor[NUM_AXES]) {
    if (mode == MotionMode::MOTION_PER_AXIS) {
        motor[X_AXIS] = lroundf(pos[Z_AXIS] * Motion1::resolution[X_AXIS]);
        motor[Y_AXIS] = lroundf(pos[Y_AXIS] * Motion1::resolution[Y_AXIS]);
        motor[Z_AXIS] = lroundf(pos[Z_AXIS] * Motion1::resolution[Z_AXIS]);
        return;
    }
    // Move them in delta mode
    float z = pos[Z_AXIS];
    for (fast8_t i = E_AXIS; i < NUM_AXES; i++) {
        motor[i] = lroundf(pos[i] * Motion1::resolution[i]);
    }
    float temp = APosY - pos[Y_AXIS];
    float opt = diagonalSquaredA - temp * temp;
    float temp2 = APosX - pos[X_AXIS];
    if ((temp = opt - temp2 * temp2) < 0) {
        // TODO: Trigger error
        return;
    }
    motor[X_AXIS] = lroundf((sqrt(temp) + z) * Motion1::resolution[X_AXIS]);

    temp = BPosY - pos[Y_AXIS];
    opt = diagonalSquaredB - temp * temp;
    temp2 = BPosX - pos[X_AXIS];
    if ((temp = opt - temp2 * temp2) < 0) {
        return;
    }
    motor[Y_AXIS] = lroundf((sqrt(temp) + z) * Motion1::resolution[Y_AXIS]);

    temp = CPosY - pos[Y_AXIS];
    opt = diagonalSquaredC - temp * temp;
    temp2 = CPosX - pos[X_AXIS];
    if ((temp = opt - temp2 * temp2) < 0) {
        return;
    }
    motor[Z_AXIS] = lroundl((sqrt(temp) + z) * Motion1::resolution[Z_AXIS]);
}

void PrinterType::disableAllowedStepper() {
    // Disabling xyz can cause carriage to drop down, therefore we never
    // disable power to these drivers.
}

float PrinterType::accelerationForMoveSteps(fast8_t axes) {
    float acceleration = 500.0f;
    FOR_ALL_AXES(i) {
        if (axes & axisBits[i]) {
            acceleration = RMath::min(acceleration, Motion1::maxAcceleration[i]);
        }
    }
    return acceleration;
}

float PrinterType::feedrateForMoveSteps(fast8_t axes) {
    float feedrate = 100.0f;
    FOR_ALL_AXES(i) {
        if (axes & axisBits[i]) {
            feedrate = RMath::min(feedrate, Motion1::maxFeedrate[i]);
        }
    }
    return feedrate;
}

void PrinterType::deactivatedTool(fast8_t id) {}

void PrinterType::activatedTool(fast8_t id) {}

void PrinterType::eepromHandle() {
    EEPROM::handlePrefix(PSTR("Delta"));
    EEPROM::handleFloat(eeprom + 0, PSTR("Diagonal [mm]"), 2, diagonal);
    EEPROM::handleFloat(eeprom + 4, PSTR("Horizontal Radius [mm]"), 2, horizontalRadius);
    EEPROM::handleFloat(eeprom + 8, PSTR("Printable Radius [mm]"), 2, printRadius);
    EEPROM::handleFloat(eeprom + 12, PSTR("Angle A [mm]"), 2, angleA);
    EEPROM::handleFloat(eeprom + 16, PSTR("Angle B [mm]"), 2, angleB);
    EEPROM::handleFloat(eeprom + 20, PSTR("Angle C [mm]"), 2, angleC);
    EEPROM::handleFloat(eeprom + 24, PSTR("Diagonal Correction A [mm]"), 2, correctionA);
    EEPROM::handleFloat(eeprom + 28, PSTR("Diagonal Correction B [mm]"), 2, correctionB);
    EEPROM::handleFloat(eeprom + 32, PSTR("Diagonal Correction C [mm]"), 2, correctionC);
    EEPROM::handleFloat(eeprom + 36, PSTR("Horiz. Radius A [mm]"), 2, radiusCorrectionA);
    EEPROM::handleFloat(eeprom + 40, PSTR("Horiz. Radius B [mm]"), 2, radiusCorrectionB);
    EEPROM::handleFloat(eeprom + 44, PSTR("Horiz. Radius C [mm]"), 2, radiusCorrectionC);
    EEPROM::handleFloat(eeprom + 48, PSTR("Home Offset A [mm]"), 2, homeOffsetA);
    EEPROM::handleFloat(eeprom + 52, PSTR("Home Offset B [mm]"), 2, homeOffsetB);
    EEPROM::handleFloat(eeprom + 56, PSTR("Home Offset C [mm]"), 2, homeOffsetC);
    EEPROM::removePrefix();
}

void PrinterType::restoreFromConfiguration() {
    diagonal = DELTA_DIAGONAL;
    horizontalRadius = DELTA_HORIZONTAL_RADIUS;
    printRadius = DELTA_PRINT_RADIUS;
    angleA = DELTA_ANGLE_A;
    angleB = DELTA_ANGLE_B;
    angleC = DELTA_ANGLE_C;
    correctionA = DELTA_CORRECTION_A;
    correctionB = DELTA_CORRECTION_B;
    correctionC = DELTA_CORRECTION_C;
    radiusCorrectionA = DELTA_RADIUS_CORRECTION_A;
    radiusCorrectionB = DELTA_RADIUS_CORRECTION_B;
    radiusCorrectionC = DELTA_RADIUS_CORRECTION_C;
    homeOffsetA = DELTA_HOME_OFFSET_A;
    homeOffsetB = DELTA_HOME_OFFSET_B;
    homeOffsetC = DELTA_HOME_OFFSET_C;
    PrinterType::updateDerived();
}

void PrinterType::init() {
    PrinterType::restoreFromConfiguration();
    eeprom = EEPROM::reserve(EEPROM_SIGNATURE_DELTA, 1, 15 * 4);
}

void PrinterType::updateDerived() {
    float radiusA = horizontalRadius + radiusCorrectionA;
    float radiusB = horizontalRadius + radiusCorrectionB;
    float radiusC = horizontalRadius + radiusCorrectionC;
    APosX = radiusA * cos(angleA * M_PI / 180.0f);
    APosY = radiusA * sin(angleA * M_PI / 180.0f);
    BPosX = radiusB * cos(angleB * M_PI / 180.0f);
    BPosY = radiusB * sin(angleB * M_PI / 180.0f);
    CPosX = radiusC * cos(angleC * M_PI / 180.0f);
    CPosY = radiusC * sin(angleC * M_PI / 180.0f);
    diagonalSquaredA = RMath::sqr(diagonal + correctionA);
    diagonalSquaredB = RMath::sqr(diagonal + correctionA);
    diagonalSquaredC = RMath::sqr(diagonal + correctionA);
    printRadiusSquared = printRadius * printRadius;
}
void PrinterType::enableMotors(fast8_t axes) {
    if (axes & 7) { // enable x,y,z as a group!
        Motion1::motors[X_AXIS]->enable();
        Motion1::motors[Y_AXIS]->enable();
        Motion1::motors[Z_AXIS]->enable();
    }
    for (fast8_t i = 3; i < NUM_AXES; i++) {
        if ((axes & axisBits[i]) != 0 && Motion1::motors[i]) {
            Motion1::motors[i]->enable();
        }
    }
    if ((axes & axisBits[E_AXIS]) != 0 && Motion1::dittoMode) {
        for (fast8_t i = 1; i <= Motion1::dittoMode; i++) {
            Tool::getTool(i)->enableMotor();
        }
    }
    Printer::unsetAllSteppersDisabled();
}
void PrinterType::setDittoMode(fast8_t count, bool mirror) {
    Motion1::dittoMode = count;
    Motion1::dittoMirror = mirror;
}

void PrinterType::transformedToOfficial(float trans[NUM_AXES], float official[NUM_AXES]) {
    Motion1::transformFromPrinter(
        trans[X_AXIS],
        trans[Y_AXIS],
        trans[Z_AXIS] - Motion1::zprobeZOffset,
        official[X_AXIS],
        official[Y_AXIS],
        official[Z_AXIS]);
    official[X_AXIS] -= Motion1::toolOffset[X_AXIS]; // Offset from active extruder or z probe
    official[Y_AXIS] -= Motion1::toolOffset[Y_AXIS];
    official[Z_AXIS] -= Motion1::toolOffset[Z_AXIS];
    for (fast8_t i = E_AXIS; i < NUM_AXES; i++) {
        official[i] = trans[i];
    }
}

void PrinterType::officialToTransformed(float official[NUM_AXES], float trans[NUM_AXES]) {
    Motion1::transformToPrinter(official[X_AXIS] + Motion1::toolOffset[X_AXIS],
                                official[Y_AXIS] + Motion1::toolOffset[Y_AXIS],
                                official[Z_AXIS] + Motion1::toolOffset[Z_AXIS],
                                trans[X_AXIS],
                                trans[Y_AXIS],
                                trans[Z_AXIS]);
    trans[Z_AXIS] += Motion1::zprobeZOffset;
    for (fast8_t i = E_AXIS; i < NUM_AXES; i++) {
        trans[i] = official[i];
    }
}
#endif