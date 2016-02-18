#include "healthparams.h"

#include "watchdatawriter.h"

HealthParams::HealthParams()
{

}

bool HealthParams::enabled() const
{
    return m_enabled;
}

void HealthParams::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

int HealthParams::height() const
{
    return m_height;
}

void HealthParams::setHeight(int height)
{
    m_height = height;
}

int HealthParams::weight() const
{
    return m_weight;
}

void HealthParams::setWeight(int weight)
{
    m_weight = weight;
}

bool HealthParams::moreActive() const
{
    return m_moreActive;
}

void HealthParams::setMoreActive(bool moreActive)
{
    m_moreActive = moreActive;
}

bool HealthParams::sleepMore() const
{
    return m_sleepMore;
}

void HealthParams::setSleepMore(bool sleepMore)
{
    m_sleepMore = sleepMore;
}

int HealthParams::age() const
{
    return m_age;
}

void HealthParams::setAge(int age)
{
    m_age = age;
}

HealthParams::Gender HealthParams::gender() const
{
    return m_gender;
}

void HealthParams::setGender(HealthParams::Gender gender)
{
    m_gender = gender;
}

QByteArray HealthParams::serialize() const
{
    QByteArray ret;
    WatchDataWriter writer(&ret);
    writer.writeLE<quint16>(m_height * 10);
    writer.writeLE<quint16>(m_weight * 100);
    writer.write<quint8>(m_enabled ? 0x01 : 0x00);
    writer.write<quint8>(m_moreActive ? 0x01 : 0x00);
    writer.write<quint8>(m_sleepMore ? 0x01 : 0x00);
    writer.write<quint8>(m_age);
    writer.write<quint8>(m_gender);
    return ret;
}

