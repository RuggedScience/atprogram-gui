#ifndef HEXSPINBOX_H
#define HEXSPINBOX_H

#pragma once

#include <QSpinBox>

// https://stackoverflow.com/questions/26581444/qspinbox-with-unsigned-int-for-hex-input

class HexSpinBox : public QSpinBox
{
    Q_OBJECT
public:
    HexSpinBox(QWidget *parent = nullptr) : QSpinBox(parent)
    {
        this->setPrefix("0x");
        this->setMaximum(255);
        this->setDisplayIntegerBase(16);
    }
protected:
    QString textFromValue(int value) const override
    {
        // Pad to the width of maximum().
        int width = QString::number(maximum(), displayIntegerBase()).size();
        return QString("%1").arg(value, width, displayIntegerBase(), QChar('0')).toUpper();
    }
};

#endif // HEXSPINBOX_H
