#ifndef HEALTHPARAMS_H
#define HEALTHPARAMS_H

#include "watchconnection.h"

class HealthParams: public PebblePacket
{
public:
    enum Gender {
        GenderFemale = 0x00,
        GenderMale = 0x01
    };

    HealthParams();

    bool enabled() const;
    void setEnabled(bool enabled);

    // In cm
    int height() const;
    void setHeight(int height);

    // In kg
    int weight() const;
    void setWeight(int weight);

    bool moreActive() const;
    void setMoreActive(bool moreActive);

    bool sleepMore() const;
    void setSleepMore(bool sleepMore);

    int age() const;
    void setAge(int age);

    Gender gender() const;
    void setGender(Gender gender);

    QByteArray serialize() const;

private:
    bool m_enabled = false;
    int m_height = 0;
    int m_weight = 0;
    bool m_moreActive = false;
    bool m_sleepMore = false;
    int m_age = 0;
    Gender m_gender = Gender::GenderFemale;

};

#endif // HEALTHPARAMS_H
