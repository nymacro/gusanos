#include "ai.h"
#include "worm.h"
#include <vector>
#include <cmath>

inline float distance(float x1, float y1, float x2, float y2)
{
    return sqrt((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
}

const float cAimAccuracy = 8.0;

void PlayerAI::getTarget()
{
    //iterate through players
    Vec pos = m_worm->getPos();
    int x = pos.x;
    int y = pos.y;
    float tmpDist = 0;
    BasePlayer* tmpTarget = NULL;
    for (std::vector<BasePlayer*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++)
    {
        //(*iter)->think();
        Vec tmpPos = (*iter)->getPos();
        int tmpX = tmpPos.x;
        int tmpY = tmpPos.y;
        if (abs(distance(x, y, tmpX, tmpY)) < tmpDist || tmpTarget == NULL)
        {
            if (*iter != this)
                tmpTarget = *iter;
        }
    }
    m_target = tmpTarget;
}

void PlayerAI::think()
{
    Player::think();

    getTarget();
    if (!m_target)
        return;

    //movement
    Vec pos = m_worm->getPos();
    int x = pos.x;
    int y = pos.y;

    Vec tmpPos = m_target->getPos();
    int tmpX = tmpPos.x;
    int tmpY = tmpPos.y;

    int dist = abs(distance(x, y, tmpX, tmpY));
    if (dist > 48)
    { 
        if (x < tmpX)
            actionStart(RIGHT);
        else
            actionStop(RIGHT);
        if (x > tmpX)
            actionStart(LEFT);
        else
            actionStop(LEFT);
    } else if (dist < 24)
    {
        if (x > tmpX)
            actionStart(RIGHT);
        else
            actionStop(RIGHT);
        if (x < tmpX)
            actionStart(LEFT);
        else
            actionStop(LEFT);
    } else
    {
        actionStop(LEFT);
        actionStop(RIGHT);
    }


    //aiming
    float curAngle = m_worm->getAngle() / m_worm->getDir();
    float targetAngle = atan2(tmpY - y, tmpX - x);

    //convert to gusanos angle system
    targetAngle -= deg2rad(90);
    targetAngle = deg2rad(180) + targetAngle;
    if (m_worm->getDir() == -1)
        targetAngle = deg2rad(180) - (targetAngle - deg2rad(180));

    while (curAngle < 0)
        curAngle += 360;
    while (curAngle >= 360)
        curAngle -= 360;

    while (rad2deg(targetAngle) < 0)
        targetAngle += deg2rad(360);
    while (rad2deg(targetAngle) >= 360)
        targetAngle -= deg2rad(360);

    //aim
    if (curAngle - cAimAccuracy > rad2deg(targetAngle))
        actionStart(UP);
    else
        actionStop(UP);

    if (curAngle + cAimAccuracy < rad2deg(targetAngle))
        actionStart(DOWN);
    else
        actionStop(DOWN);

    //shooting
    if (curAngle - cAimAccuracy < rad2deg(targetAngle) && curAngle + cAimAccuracy > rad2deg(targetAngle))
    {
	    //aimed accuratley
	    actionStart(FIRE);
    } else
	    actionStop(FIRE);
}
