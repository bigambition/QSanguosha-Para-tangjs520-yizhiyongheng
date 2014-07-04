#include "sp.h"
#include "client.h"
#include "general.h"
#include "skill.h"
#include "standard-skillcards.h"
#include "engine.h"
#include "maneuvering.h"

class SPMoonSpearSkill: public WeaponSkill {
public:
    SPMoonSpearSkill(): WeaponSkill("sp_moonspear") {
        events << CardUsed << CardResponded;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (player->getPhase() != Player::NotActive)
            return false;

        const Card *card = NULL;
        if (triggerEvent == CardUsed) {
            CardUseStruct card_use = data.value<CardUseStruct>();
            card = card_use.card;
        } else if (triggerEvent == CardResponded) {
            card = data.value<CardResponseStruct>().m_card;
        }

        if (card == NULL || !card->isBlack()
            || (card->getHandlingMethod() != Card::MethodUse && card->getHandlingMethod() != Card::MethodResponse))
            return false;

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *tmp, room->getAlivePlayers()) {
            if (player->inMyAttackRange(tmp))
                targets << tmp;
        }
        if (targets.isEmpty()) return false;

        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@sp_moonspear", true, true);
        if (!target) return false;
        room->setEmotion(player, "weapon/moonspear");
        if (!room->askForCard(target, "jink", "@moon-spear-jink", QVariant(), Card::MethodResponse, player))
            room->damage(DamageStruct(objectName(), player, target));
        return false;
    }
};

SPMoonSpear::SPMoonSpear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("sp_moonspear");
}

class Jilei: public TriggerSkill {
public:
    Jilei(): TriggerSkill("jilei") {
        events << Damaged;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *yangxiu, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *current = room->getCurrent();
        if (!current || current->getPhase() == Player::NotActive || current->isDead() || !damage.from)
            return false;

        if (room->askForSkillInvoke(yangxiu, objectName(), data)) {
            QString choice = room->askForChoice(yangxiu, objectName(), "BasicCard+EquipCard+TrickCard");
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#Jilei";
            log.from = damage.from;
            log.arg = choice;
            room->sendLog(log);

            QStringList jilei_list = damage.from->tag[objectName()].toStringList();
            if (jilei_list.contains(choice)) return false;
            jilei_list.append(choice);
            damage.from->tag[objectName()] = QVariant::fromValue(jilei_list);
            QString _type = choice + "|.|.|hand"; // Handcards only
            room->setPlayerCardLimitation(damage.from, "use,response,discard", _type, true);

            QString type_name = choice.replace("Card", "").toLower();
            if (damage.from->getMark("@jilei_" + type_name) == 0)
                room->addPlayerMark(damage.from, "@jilei_" + type_name);
        }

        return false;
    }
};

class JileiClear: public TriggerSkill {
public:
    JileiClear(): TriggerSkill("#jilei-clear") {
        events << EventPhaseChanging << Death;
    }

    virtual int getPriority(TriggerEvent) const{
        return 5;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const{
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != target || target != room->getCurrent())
                return false;
        }
        QList<ServerPlayer *> players = room->getAllPlayers();
        foreach (ServerPlayer *player, players) {
            QStringList jilei_list = player->tag["jilei"].toStringList();
            if (!jilei_list.isEmpty()) {
                LogMessage log;
                log.type = "#JileiClear";
                log.from = player;
                room->sendLog(log);

                foreach (QString jilei_type, jilei_list) {
                    room->removePlayerCardLimitation(player, "use,response,discard", jilei_type + "|.|.|hand$1");
                    QString type_name = jilei_type.replace("Card", "").toLower();
                    room->setPlayerMark(player, "@jilei_" + type_name, 0);
                }
                player->tag.remove("jilei");
            }
        }

        return false;
    }
};

class Danlao: public TriggerSkill {
public:
    Danlao(): TriggerSkill("danlao") {
        events << TargetConfirmed << CardEffected;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.to.length() <= 1 || !use.to.contains(player)
                || !use.card->isKindOf("TrickCard")
                || !room->askForSkillInvoke(player, objectName(), data))
                return false;

            room->broadcastSkillInvoke(objectName());
            player->setFlags("-DanlaoTarget");
            player->setFlags("DanlaoTarget");
            player->drawCards(1, objectName());
            if (player->isAlive() && player->hasFlag("DanlaoTarget")) {
                player->setFlags("-DanlaoTarget");
                use.nullified_list << player->objectName();
                data = QVariant::fromValue(use);
            }
        } else {
            if (!player->isAlive() || !player->hasSkill(objectName()))
                return false;

            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (player->tag["Danlao"].isNull() || player->tag["Danlao"].toString() != effect.card->toString())
                return false;

            player->tag["Danlao"] = QVariant(QString());

            LogMessage log;
            log.type = "#DanlaoAvoid";
            log.from = player;
            log.arg = effect.card->objectName();
            log.arg2 = objectName();
            room->sendLog(log);

            return true;
        }

        return false;
    }
};

Yongsi::Yongsi(): TriggerSkill("yongsi") {
    events << DrawNCards << EventPhaseStart;
    frequency = Compulsory;
}

int Yongsi::getKingdoms(ServerPlayer *yuanshu) const{
    QSet<QString> kingdom_set;
    Room *room = yuanshu->getRoom();
    foreach (ServerPlayer *p, room->getAlivePlayers())
        kingdom_set << p->getKingdom();

    return kingdom_set.size();
}

bool Yongsi::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *yuanshu, QVariant &data) const{
    if (triggerEvent == DrawNCards) {
        int x = getKingdoms(yuanshu);
        data = data.toInt() + x;

        Room *room = yuanshu->getRoom();
        LogMessage log;
        log.type = "#YongsiGood";
        log.from = yuanshu;
        log.arg = QString::number(x);
        log.arg2 = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(yuanshu, objectName());

        room->broadcastSkillInvoke("yongsi", x % 2 + 1);
    } else if (triggerEvent == EventPhaseStart && yuanshu->getPhase() == Player::Discard) {
        int x = getKingdoms(yuanshu);
        LogMessage log;
        log.type = yuanshu->getCardCount() > x ? "#YongsiBad" : "#YongsiWorst";
        log.from = yuanshu;
        log.arg = QString::number(log.type == "#YongsiBad" ? x : yuanshu->getCardCount());
        log.arg2 = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(yuanshu, objectName());

        room->broadcastSkillInvoke("yongsi", x % 2 + 1);

        if (x > 0)
            room->askForDiscard(yuanshu, "yongsi", x, x, false, true);
    }

    return false;
}

class WeidiViewAsSkill: public ViewAsSkill {
public:
    WeidiViewAsSkill(): ViewAsSkill("weidi") {
    }

    static QList<const ViewAsSkill *> getLordViewAsSkills(const Player *player) {
        const Player *lord = NULL;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->isLord()) {
                lord = p;
                break;
            }
        }
        if (!lord) return QList<const ViewAsSkill *>();

        QList<const ViewAsSkill *> vs_skills;
        foreach (const Skill *skill, lord->getVisibleSkillList()) {
            if (skill->isLordSkill() && player->hasLordSkill(skill->objectName())) {
                const ViewAsSkill *vs = ViewAsSkill::parseViewAsSkill(skill);
                if (vs)
                    vs_skills << vs;
            }
        }
        return vs_skills;
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        QList<const ViewAsSkill *> vs_skills = getLordViewAsSkills(player);
        foreach (const ViewAsSkill *skill, vs_skills) {
            if (skill->isEnabledAtPlay(player))
                return true;
        }
        return false;
    }

    virtual bool isEnabledAtResponse(const Player *player, const QString &pattern) const{
        QList<const ViewAsSkill *> vs_skills = getLordViewAsSkills(player);
        foreach (const ViewAsSkill *skill, vs_skills) {
            if (skill->isEnabledAtResponse(player, pattern))
                return true;
        }
        return false;
    }

    virtual bool isEnabledAtNullification(const ServerPlayer *player) const{
        QList<const ViewAsSkill *> vs_skills = getLordViewAsSkills(player);
        foreach (const ViewAsSkill *skill, vs_skills) {
            if (skill->isEnabledAtNullification(player))
                return true;
        }
        return false;
    }

    virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const{
        QString skill_name = Self->tag["weidi"].toString();
        if (skill_name.isEmpty()) return false;
        const ViewAsSkill *vs_skill = Sanguosha->getViewAsSkill(skill_name);
        if (vs_skill) return vs_skill->viewFilter(selected, to_select);
        return false;
    }

    virtual const Card *viewAs(const QList<const Card *> &cards) const{
        QString skill_name = Self->tag["weidi"].toString();
        if (skill_name.isEmpty()) return NULL;
        const ViewAsSkill *vs_skill = Sanguosha->getViewAsSkill(skill_name);
        if (vs_skill) return vs_skill->viewAs(cards);
        return NULL;
    }
};

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCommandLinkButton>

WeidiDialog *WeidiDialog::getInstance() {
    static WeidiDialog *instance;
    if (instance == NULL)
        instance = new WeidiDialog();

    return instance;
}

WeidiDialog::WeidiDialog() {
    setObjectName("weidi");
    setWindowTitle(Sanguosha->translate("weidi"));
    group = new QButtonGroup(this);

    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectSkill(QAbstractButton *)));
}

void WeidiDialog::popup() {
    Self->tag.remove(objectName());
    foreach (QAbstractButton *button, group->buttons()) {
        button_layout->removeWidget(button);
        group->removeButton(button);
        delete button;
    }

    QList<const ViewAsSkill *> vs_skills = WeidiViewAsSkill::getLordViewAsSkills(Self);
    int count = 0;
    QString name;
    foreach (const ViewAsSkill *skill, vs_skills) {
        QAbstractButton *button = createSkillButton(skill->objectName());
        button->setEnabled(skill->isAvailable(Self, Sanguosha->getCurrentCardUseReason(),
            Sanguosha->getCurrentCardUsePattern()));
        if (button->isEnabled()) {
            ++count;
            name = skill->objectName();
        }
        button_layout->addWidget(button);
    }

    if (count == 0) {
        emit onButtonClick();
        return;
    } else if (count == 1) {
        Self->tag[objectName()] = name;
        emit onButtonClick();
        return;
    }

    exec();
}

void WeidiDialog::selectSkill(QAbstractButton *button) {
    Self->tag[objectName()] = button->objectName();
    emit onButtonClick();
    accept();
}

QAbstractButton *WeidiDialog::createSkillButton(const QString &skill_name) {
    const Skill *skill = Sanguosha->getSkill(skill_name);
    if (!skill) return NULL;

    QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(skill_name));
    button->setObjectName(skill_name);
    button->setToolTip(skill->getDescription());

    group->addButton(button);
    return button;
}

class Weidi: public GameStartSkill {
public:
    Weidi(): GameStartSkill("weidi") {
        frequency = Compulsory;
        view_as_skill = new WeidiViewAsSkill;
    }

    virtual void onGameStart(ServerPlayer *) const{
        return;
    }

    virtual QDialog *getDialog() const{
        return WeidiDialog::getInstance();
    }
};

class Yicong: public DistanceSkill {
public:
    Yicong(): DistanceSkill("yicong") {
    }

    virtual int getCorrect(const Player *from, const Player *to) const{
        int correct = 0;
        if (from->hasSkill(objectName()) && from->getHp() > 2)
            --correct;
        if (to->hasSkill(objectName()) && to->getHp() <= 2)
            ++correct;

        return correct;
    }
};

class YicongEffect: public TriggerSkill {
public:
    YicongEffect(): TriggerSkill("#yicong-effect") {
        events << HpChanged;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        int hp = player->getHp();
        int index = 0;
        int reduce = 0;
        if (data.canConvert<RecoverStruct>()) {
            int rec = data.value<RecoverStruct>().recover;
            if (hp > 2 && hp - rec < 2)
                index = 1;
        } else {
            if (data.canConvert<DamageStruct>()) {
                DamageStruct damage = data.value<DamageStruct>();
                reduce = damage.damage;
            } else if (!data.isNull()) {
                reduce = data.toInt();
            }
            if (hp <= 2 && hp + reduce > 2)
                index = 2;
        }

        if (player->getGeneralName() == "gongsunzan"
            || (player->getGeneralName() != "st_gongsunzan" && player->getGeneral2Name() == "gongsunzan"))
            index += 2;

        if (index > 0)
            room->broadcastSkillInvoke("yicong", index);

        return false;
    }
};

class Danji: public PhaseChangeSkill {
public:
    Danji(): PhaseChangeSkill("danji") { // What a silly skill!
        frequency = Wake;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return PhaseChangeSkill::triggerable(target)
               && target->getPhase() == Player::Start
               && target->getMark("danji") == 0
               && target->getHandcardNum() > target->getHp();
    }

    virtual bool onPhaseChange(ServerPlayer *guanyu) const{
        Room *room = guanyu->getRoom();
        ServerPlayer *the_lord = room->getLord();
        if (the_lord && (the_lord->getGeneralName().contains("caocao") || the_lord->getGeneral2Name().contains("caocao"))) {
            room->notifySkillInvoked(guanyu, objectName());

            LogMessage log;
            log.type = "#DanjiWake";
            log.from = guanyu;
            log.arg = QString::number(guanyu->getHandcardNum());
            log.arg2 = QString::number(guanyu->getHp());
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->doLightbox("$DanjiAnimate", 5000);

            room->setPlayerMark(guanyu, "danji", 1);
            if (room->changeMaxHpForAwakenSkill(guanyu) && guanyu->getMark("danji") == 1)
                room->acquireSkill(guanyu, "mashu");
        }

        return false;
    }
};

YuanhuCard::YuanhuCard() {
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool YuanhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (!targets.isEmpty())
        return false;

    const Card *card = Sanguosha->getCard(subcards.first());
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    int equip_index = static_cast<int>(equip->location());
    return to_select->getEquip(equip_index) == NULL;
}

void YuanhuCard::onUse(Room *room, const CardUseStruct &card_use) const{
    int index = -1;
    if (card_use.to.first() == card_use.from)
        index = 5;
    else if (card_use.to.first()->getGeneralName().contains("caocao"))
        index = 4;
    else {
        const Card *card = Sanguosha->getCard(card_use.card->getSubcards().first());
        if (card->isKindOf("Weapon"))
            index = 1;
        else if (card->isKindOf("Armor"))
            index = 2;
        else if (card->isKindOf("Horse"))
            index = 3;
    }
    room->broadcastSkillInvoke("yuanhu", index);
    SkillCard::onUse(room, card_use);
}

void YuanhuCard::onEffect(const CardEffectStruct &effect) const{
    ServerPlayer *caohong = effect.from;
    Room *room = caohong->getRoom();
    room->moveCardTo(this, caohong, effect.to, Player::PlaceEquip,
                     CardMoveReason(CardMoveReason::S_REASON_PUT, caohong->objectName(), "yuanhu", QString()));

    const Card *card = Sanguosha->getCard(subcards.first());

    LogMessage log;
    log.type = "$ZhijianEquip";
    log.from = effect.to;
    log.card_str = QString::number(card->getEffectiveId());
    room->sendLog(log);

    if (card->isKindOf("Weapon")) {
      QList<ServerPlayer *> targets;
      foreach (ServerPlayer *p, room->getAllPlayers()) {
          if (effect.to->distanceTo(p) == 1 && caohong->canDiscard(p, "hej"))
              targets << p;
      }
      if (!targets.isEmpty()) {
          ServerPlayer *to_dismantle = room->askForPlayerChosen(caohong, targets, "yuanhu", "@yuanhu-discard:" + effect.to->objectName());
          int card_id = room->askForCardChosen(caohong, to_dismantle, "hej", "yuanhu", false, Card::MethodDiscard);
          room->throwCard(Sanguosha->getCard(card_id), to_dismantle, caohong);
      }
    } else if (card->isKindOf("Armor")) {
        effect.to->drawCards(1, "yuanhu");
    } else if (card->isKindOf("Horse")) {
        room->recover(effect.to, RecoverStruct(effect.from));
    }
}

class YuanhuViewAsSkill: public OneCardViewAsSkill {
public:
    YuanhuViewAsSkill(): OneCardViewAsSkill("yuanhu") {
        filter_pattern = "EquipCard";
        response_pattern = "@@yuanhu";
    }

    virtual const Card *viewAs(const Card *originalcard) const{
        YuanhuCard *first = new YuanhuCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Yuanhu: public PhaseChangeSkill {
public:
    Yuanhu(): PhaseChangeSkill("yuanhu") {
        view_as_skill = new YuanhuViewAsSkill;
    }

    virtual bool onPhaseChange(ServerPlayer *target) const{
        Room *room = target->getRoom();
        if (target->getPhase() == Player::Finish && !target->isNude())
            room->askForUseCard(target, "@@yuanhu", "@yuanhu-equip", -1, Card::MethodNone);
        return false;
    }
};

XuejiCard::XuejiCard() {
}

bool XuejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (targets.length() >= Self->getLostHp())
        return false;

    if (to_select == Self)
        return false;

    int range_fix = 0;
    if (Self->getWeapon() && Self->getWeapon()->getEffectiveId() == getEffectiveId()) {
        const Weapon *weapon = qobject_cast<const Weapon *>(Self->getWeapon()->getRealCard());
        range_fix += weapon->getRange() - Self->getAttackRange(false);
    } else if (Self->getOffensiveHorse() && Self->getOffensiveHorse()->getEffectiveId() == getEffectiveId())
        range_fix += 1;

    return Self->inMyAttackRange(to_select, range_fix);
}

void XuejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    DamageStruct damage;
    damage.from = source;
    damage.reason = "xueji";

    foreach (ServerPlayer *p, targets) {
        damage.to = p;
        room->damage(damage);
    }
    foreach (ServerPlayer *p, targets) {
        if (p->isAlive())
            p->drawCards(1, "xueji");
    }
}

class Xueji: public OneCardViewAsSkill {
public:
    Xueji(): OneCardViewAsSkill("xueji") {
        filter_pattern = ".|red!";
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return player->getLostHp() > 0 && player->canDiscard(player, "he") && !player->hasUsed("XuejiCard");
    }

    virtual const Card *viewAs(const Card *originalcard) const{
        XuejiCard *first = new XuejiCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Huxiao: public TargetModSkill {
public:
    Huxiao(): TargetModSkill("huxiao") {
    }

    virtual int getResidueNum(const Player *from, const Card *) const{
        if (from->hasSkill(objectName()))
            return from->getMark(objectName());
        else
            return 0;
    }
};

class HuxiaoCount: public TriggerSkill {
public:
    HuxiaoCount(): TriggerSkill("#huxiao-count") {
        events << SlashMissed << EventPhaseChanging;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == SlashMissed) {
            if (player->getPhase() == Player::Play)
                room->addPlayerMark(player, "huxiao");
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.from == Player::Play)
                if (player->getMark("huxiao") > 0)
                    room->setPlayerMark(player, "huxiao", 0);
        }

        return false;
    }
};

class HuxiaoClear: public DetachEffectSkill {
public:
    HuxiaoClear(): DetachEffectSkill("huxiao") {
    }

    virtual void onSkillDetached(Room *room, ServerPlayer *player) const{
        room->setPlayerMark(player, "huxiao", 0);
    }
};

class WujiCount: public TriggerSkill {
public:
    WujiCount(): TriggerSkill("#wuji-count") {
        events << PreDamageDone << EventPhaseChanging;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == PreDamageDone) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive() && damage.from == room->getCurrent() && damage.from->getMark("wuji") == 0)
                room->addPlayerMark(damage.from, "wuji_damage", damage.damage);
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                if (player->getMark("wuji_damage") > 0)
                    room->setPlayerMark(player, "wuji_damage", 0);
        }

        return false;
    }
};

class Wuji: public PhaseChangeSkill {
public:
    Wuji(): PhaseChangeSkill("wuji") {
        frequency = Wake;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return PhaseChangeSkill::triggerable(target)
               && target->getPhase() == Player::Finish
               && target->getMark("wuji") == 0
               && target->getMark("wuji_damage") >= 3;
    }

    virtual bool onPhaseChange(ServerPlayer *player) const{
        Room *room = player->getRoom();
        room->notifySkillInvoked(player, objectName());

        LogMessage log;
        log.type = "#WujiWake";
        log.from = player;
        log.arg = QString::number(player->getMark("wuji_damage"));
        log.arg2 = objectName();
        room->sendLog(log);

        room->broadcastSkillInvoke(objectName());
        room->doLightbox("$WujiAnimate", 4000);

        room->setPlayerMark(player, "wuji", 1);
        if (room->changeMaxHpForAwakenSkill(player, 1)) {
            room->recover(player, RecoverStruct(player));
            if (player->getMark("wuji") == 1)
                room->detachSkillFromPlayer(player, "huxiao");
        }

        return false;
    }
};

class Baobian: public TriggerSkill {
public:
    Baobian(): TriggerSkill("baobian") {
        events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventLoseSkill) {
            if (data.toString() == objectName()) {
                QStringList baobian_skills = player->tag["BaobianSkills"].toStringList();
                QStringList detachList;
                foreach (QString skill_name, baobian_skills)
                    detachList.append("-" + skill_name);
                room->handleAcquireDetachSkills(player, detachList);
                player->tag["BaobianSkills"] = QVariant();
            }
            return false;
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != objectName()) return false;
        }

        if (!player->isAlive() || !player->hasSkill(objectName(), true)) return false;

        acquired_skills.clear();
        detached_skills.clear();
        BaobianChange(room, player, 1, "shensu");
        BaobianChange(room, player, 2, "paoxiao");
        BaobianChange(room, player, 3, "tiaoxin");
        if (!acquired_skills.isEmpty() || !detached_skills.isEmpty())
            room->handleAcquireDetachSkills(player, acquired_skills + detached_skills);
        return false;
    }

private:
    void BaobianChange(Room *room, ServerPlayer *player, int hp, const QString &skill_name) const{
        QStringList baobian_skills = player->tag["BaobianSkills"].toStringList();
        if (player->getHp() <= hp) {
            if (!baobian_skills.contains(skill_name)) {
                room->notifySkillInvoked(player, "baobian");
                if (player->getHp() == hp)
                    room->broadcastSkillInvoke("baobian", 4 - hp);
                acquired_skills.append(skill_name);
                baobian_skills << skill_name;
            }
        } else {
            if (baobian_skills.contains(skill_name)) {
                detached_skills.append("-" + skill_name);
                baobian_skills.removeOne(skill_name);
            }
        }
        player->tag["BaobianSkills"] = QVariant::fromValue(baobian_skills);
    }

    mutable QStringList acquired_skills, detached_skills;
};

BifaCard::BifaCard() {
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool BifaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    return targets.isEmpty() && to_select->getPile("bifa").isEmpty() && to_select != Self;
}

void BifaCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    ServerPlayer *target = targets.first();
    target->tag["BifaSource" + QString::number(getEffectiveId())] = QVariant::fromValue(source);
    room->broadcastSkillInvoke("bifa", 1);
    target->addToPile("bifa", this, false);
}

class BifaViewAsSkill: public OneCardViewAsSkill {
public:
    BifaViewAsSkill(): OneCardViewAsSkill("bifa") {
        filter_pattern = ".|.|.|hand";
        response_pattern = "@@bifa";
    }

    virtual const Card *viewAs(const Card *originalcard) const{
        Card *card = new BifaCard;
        card->addSubcard(originalcard);
        return card;
    }
};

class Bifa: public TriggerSkill {
public:
    Bifa(): TriggerSkill("bifa") {
        events << EventPhaseStart;
        view_as_skill = new BifaViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const{
        if (TriggerSkill::triggerable(player) && player->getPhase() == Player::Finish && !player->isKongcheng()) {
            room->askForUseCard(player, "@@bifa", "@bifa-remove", -1, Card::MethodNone);
        } else if (player->getPhase() == Player::RoundStart && player->getPile("bifa").length() > 0) {
            int card_id = player->getPile("bifa").first();
            ServerPlayer *chenlin = player->tag["BifaSource" + QString::number(card_id)].value<ServerPlayer *>();
            QList<int> ids;
            ids << card_id;

            LogMessage log;
            log.type = "$BifaView";
            log.from = player;
            log.card_str = QString::number(card_id);
            log.arg = "bifa";
            room->sendLog(log, player);

            room->fillAG(ids, player);
            const Card *cd = Sanguosha->getCard(card_id);
            QString pattern;
            if (cd->isKindOf("BasicCard"))
                pattern = "BasicCard";
            else if (cd->isKindOf("TrickCard"))
                pattern = "TrickCard";
            else if (cd->isKindOf("EquipCard"))
                pattern = "EquipCard";
            QVariant data_for_ai = QVariant::fromValue(pattern);
            pattern.append("|.|.|hand");
            const Card *to_give = NULL;
            if (!player->isKongcheng() && chenlin && chenlin->isAlive())
                to_give = room->askForCard(player, pattern, "@bifa-give", data_for_ai, Card::MethodNone, chenlin);
            if (chenlin && to_give) {
                room->broadcastSkillInvoke(objectName(), 2);
                chenlin->obtainCard(to_give, false, CardMoveReason::S_REASON_GIVE);
                CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(), "bifa", QString());
                room->obtainCard(player, cd, reason, false);
            } else {
                room->broadcastSkillInvoke(objectName(), 3);
                CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
                room->throwCard(cd, reason, NULL);
                room->loseHp(player);
            }
            room->clearAG(player);
            player->tag.remove("BifaSource" + QString::number(card_id));
        }
        return false;
    }
};

SongciCard::SongciCard() {
    mute = true;
}

bool SongciCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    return targets.isEmpty() && to_select->getMark("songci" + Self->objectName()) == 0 && to_select->getHandcardNum() != to_select->getHp();
}

void SongciCard::onEffect(const CardEffectStruct &effect) const{
    int handcard_num = effect.to->getHandcardNum();
    int hp = effect.to->getHp();
    effect.to->gainMark("@songci");
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.to, "songci" + effect.from->objectName());
    if (handcard_num > hp) {
        room->broadcastSkillInvoke("songci", 2);
        room->askForDiscard(effect.to, "songci", 2, 2, false, true);
    } else if (handcard_num < hp) {
        room->broadcastSkillInvoke("songci", 1);
        effect.to->drawCards(2, "songci");
    }
}

class SongciViewAsSkill: public ZeroCardViewAsSkill {
public:
    SongciViewAsSkill(): ZeroCardViewAsSkill("songci") {
    }

    virtual const Card *viewAs() const{
        return new SongciCard;
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        if (player->getMark("songci" + player->objectName()) == 0 && player->getHandcardNum() != player->getHp()) return true;
        foreach (const Player *sib, player->getAliveSiblings())
            if (sib->getMark("songci" + player->objectName()) == 0 && sib->getHandcardNum() != sib->getHp())
                return true;
        return false;
    }
};

class Songci: public TriggerSkill {
public:
    Songci(): TriggerSkill("songci") {
        events << Death;
        view_as_skill = new SongciViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target && target->hasSkill(objectName());
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DeathStruct death = data.value<DeathStruct>();
        if (death.who != player) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getMark("@songci") > 0)
                room->setPlayerMark(p, "@songci", 0);
            if (p->getMark("songci" + player->objectName()) > 0)
                room->setPlayerMark(p, "songci" + player->objectName(), 0);
        }
        return false;
    }
};

class Xiuluo: public PhaseChangeSkill {
public:
    Xiuluo(): PhaseChangeSkill("xiuluo") {
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return PhaseChangeSkill::triggerable(target)
               && target->getPhase() == Player::Start
               && target->canDiscard(target, "h")
               && hasDelayedTrick(target);
    }

    virtual bool onPhaseChange(ServerPlayer *target) const{
        Room *room = target->getRoom();
        while (hasDelayedTrick(target) && target->canDiscard(target, "h")) {
            QStringList suits;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (!suits.contains(jcard->getSuitString()))
                    suits << jcard->getSuitString();
            }

            const Card *card = room->askForCard(target, QString(".|%1|.|hand").arg(suits.join(",")),
                                                "@xiuluo", QVariant(), objectName());
            if (!card || !hasDelayedTrick(target)) break;
            room->broadcastSkillInvoke(objectName());

            QList<int> avail_list, other_list;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (jcard->isKindOf("SkillCard")) continue;
                if (jcard->getSuit() == card->getSuit())
                    avail_list << jcard->getEffectiveId();
                else
                    other_list << jcard->getEffectiveId();
            }
            room->fillAG(avail_list + other_list, NULL, other_list);
            int id = room->askForAG(target, avail_list, false, objectName());
            room->clearAG();
            room->throwCard(id, NULL);
        }

        return false;
    }

private:
    static bool hasDelayedTrick(const ServerPlayer *target) {
        foreach (const Card *card, target->getJudgingArea())
            if (!card->isKindOf("SkillCard")) return true;
        return false;
    }
};

class Shenwei: public DrawCardsSkill {
public:
    Shenwei(): DrawCardsSkill("#shenwei-draw") {
        frequency = Compulsory;
    }

    virtual int getDrawNum(ServerPlayer *player, int n) const{
        Room *room = player->getRoom();
        room->broadcastSkillInvoke("shenwei");
        room->notifySkillInvoked(player, "shenwei");
        LogMessage log;
        log.type = "#TriggerSkill";
        log.from = player;
        log.arg = "shenwei";
        room->sendLog(log);

        return n + 2;
    }
};

class ShenweiKeep: public MaxCardsSkill {
public:
    ShenweiKeep(): MaxCardsSkill("shenwei") {
    }

    virtual int getExtra(const Player *target) const{
        if (target->hasSkill(objectName()))
            return 2;
        else
            return 0;
    }
};

class Shenji: public TargetModSkill {
public:
    Shenji(): TargetModSkill("shenji") {
    }

    virtual int getExtraTargetNum(const Player *from, const Card *) const{
        if (from->hasSkill(objectName()) && from->getWeapon() == NULL)
            return 2;
        else
            return 0;
    }
};

class Xingwu: public TriggerSkill {
public:
    Xingwu(): TriggerSkill("xingwu") {
        events << PreCardUsed << CardResponded << EventPhaseStart << CardsMoveOneTime;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == PreCardUsed || triggerEvent == CardResponded) {
            const Card *card = NULL;
            if (triggerEvent == PreCardUsed)
                card = data.value<CardUseStruct>().card;
            else {
                CardResponseStruct response = data.value<CardResponseStruct>();
                if (response.m_isUse)
                   card = response.m_card;
            }
            if (card && card->getTypeId() != Card::TypeSkill && card->getHandlingMethod() == Card::MethodUse) {
                int n = player->getMark(objectName());
                if (card->isBlack())
                    n |= 1;
                else if (card->isRed())
                    n |= 2;
                player->setMark(objectName(), n);
            }
        } else if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() == Player::Discard) {
                int n = player->getMark(objectName());
                bool red_avail = ((n & 2) == 0), black_avail = ((n & 1) == 0);
                if (player->isKongcheng() || (!red_avail && !black_avail))
                    return false;
                QString pattern = ".|.|.|hand";
                if (red_avail != black_avail)
                    pattern = QString(".|%1|.|hand").arg(red_avail ? "red" : "black");
                const Card *card = room->askForCard(player, pattern, "@xingwu", QVariant(), Card::MethodNone);
                if (card) {
                    room->notifySkillInvoked(player, objectName());
                    room->broadcastSkillInvoke(objectName(), 1);

                    LogMessage log;
                    log.type = "#InvokeSkill";
                    log.from = player;
                    log.arg = objectName();
                    room->sendLog(log);

                    player->addToPile(objectName(), card);
                }
            } else if (player->getPhase() == Player::RoundStart) {
                player->setMark(objectName(), 0);
            }
        } else if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceSpecial && player->getPile(objectName()).length() >= 3) {
                player->clearOnePrivatePile(objectName());
                QList<ServerPlayer *> males;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->isMale())
                        males << p;
                }
                if (males.isEmpty()) return false;

                ServerPlayer *target = room->askForPlayerChosen(player, males, objectName(), "@xingwu-choose");
                room->broadcastSkillInvoke(objectName(), 2);
                room->damage(DamageStruct(objectName(), player, target, 2));

                if (!player->isAlive()) return false;
                QList<const Card *> equips = target->getEquips();
                if (!equips.isEmpty()) {
                    DummyCard *dummy = new DummyCard;
                    foreach (const Card *equip, equips) {
                        if (player->canDiscard(target, equip->getEffectiveId()))
                            dummy->addSubcard(equip);
                    }
                    if (dummy->subcardsLength() > 0)
                        room->throwCard(dummy, target, player);
                    delete dummy;
                }
            }
        }
        return false;
    }
};

class Luoyan: public TriggerSkill {
public:
    Luoyan(): TriggerSkill("luoyan") {
        events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventLoseSkill && data.toString() == objectName()) {
            room->handleAcquireDetachSkills(player, "-tianxiang|-liuli", true);
        } else if (triggerEvent == EventAcquireSkill && data.toString() == objectName()) {
            if (!player->getPile("xingwu").isEmpty()) {
                room->notifySkillInvoked(player, objectName());
                room->handleAcquireDetachSkills(player, "tianxiang|liuli");
            }
        } else if (triggerEvent == CardsMoveOneTime && player->isAlive() && player->hasSkill(objectName(), true)) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceSpecial && move.to_pile_name == "xingwu") {
                if (player->getPile("xingwu").length() == 1) {
                    room->notifySkillInvoked(player, objectName());
                    room->handleAcquireDetachSkills(player, "tianxiang|liuli");
                }
            } else if (move.from == player && move.from_places.contains(Player::PlaceSpecial)
                       && move.from_pile_names.contains("xingwu")) {
                if (player->getPile("xingwu").isEmpty())
                    room->handleAcquireDetachSkills(player, "-tianxiang|-liuli", true);
            }
        }
        return false;
    }
};

class Yanyu: public TriggerSkill {
public:
    Yanyu(): TriggerSkill("yanyu") {
        events << EventPhaseStart << BeforeCardsMove << EventPhaseChanging;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventPhaseStart) {
            ServerPlayer *xiahou = room->findPlayerBySkillName(objectName());
            if (xiahou && player->getPhase() == Player::Play) {
                if (!xiahou->canDiscard(xiahou, "he")) return false;
                const Card *card = room->askForCard(xiahou, "..", "@yanyu-discard", QVariant(), objectName());
                if (card)
                    xiahou->addMark("YanyuDiscard" + QString::number(card->getTypeId()), 3);
            }
        } else if (triggerEvent == BeforeCardsMove && TriggerSkill::triggerable(player)) {
            ServerPlayer *current = room->getCurrent();
            if (!current || current->getPhase() != Player::Play) return false;
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to_place == Player::DiscardPile) {
                QList<int> ids, disabled;
                QList<int> all_ids = move.card_ids;
                foreach (int id, move.card_ids) {
                    const Card *card = Sanguosha->getCard(id);
                    if (player->getMark("YanyuDiscard" + QString::number(card->getTypeId())) > 0)
                        ids << id;
                    else
                        disabled << id;
                }
                if (ids.isEmpty()) return false;
                while (!ids.isEmpty()) {
                    room->fillAG(all_ids, player, disabled);
                    bool only = (all_ids.length() == 1);
                    int card_id = -1;
                    if (only)
                        card_id = ids.first();
                    else
                        card_id = room->askForAG(player, ids, true, objectName());
                    room->clearAG(player);
                    if (card_id == -1) break;
                    if (only)
                        player->setMark("YanyuOnlyId", card_id + 1); // For AI
                    const Card *card = Sanguosha->getCard(card_id);
                    ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                                                                    QString("@yanyu-give:::%1:%2\\%3").arg(card->objectName())
                                                                                                      .arg(card->getSuitString() + "_char")
                                                                                                      .arg(card->getNumberString()),
                                                                    only, true);
                    player->setMark("YanyuOnlyId", 0);
                    if (target) {
                        player->removeMark("YanyuDiscard" + QString::number(card->getTypeId()));
                        Player::Place place = move.from_places.at(move.card_ids.indexOf(card_id));
                        QList<int> _card_id;
                        _card_id << card_id;
                        move.removeCardIds(_card_id);
                        data = QVariant::fromValue(move);
                        ids.removeOne(card_id);
                        disabled << card_id;
                        foreach (int id, ids) {
                            const Card *card = Sanguosha->getCard(id);
                            if (player->getMark("YanyuDiscard" + QString::number(card->getTypeId())) == 0) {
                                ids.removeOne(id);
                                disabled << id;
                            }
                        }
                        if (move.from && move.from->objectName() == target->objectName() && place != Player::PlaceTable) {
                            // just indicate which card she chose.
                            LogMessage log;
                            log.type = "$MoveCard";
                            log.from = target;
                            log.to << target;
                            log.card_str = QString::number(card_id);
                            room->sendLog(log);
                        }
                        target->obtainCard(card);
                    } else
                        break;
                }
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    p->setMark("YanyuDiscard1", 0);
                    p->setMark("YanyuDiscard2", 0);
                    p->setMark("YanyuDiscard3", 0);
                }
            }
        }
        return false;
    }
};

class Xiaode: public TriggerSkill {
public:
    Xiaode(): TriggerSkill("xiaode") {
        events << BuryVictim;
    }

    virtual int getPriority(TriggerEvent) const{
        return -2;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const{
        ServerPlayer *xiahoushi = room->findPlayerBySkillName(objectName());
        if (!xiahoushi || !xiahoushi->tag["XiaodeSkill"].toString().isEmpty()) return false;
        QStringList skill_list = xiahoushi->tag["XiaodeVictimSkills"].toStringList();
        if (skill_list.isEmpty()) return false;
        if (!room->askForSkillInvoke(xiahoushi, objectName(), QVariant::fromValue(skill_list))) return false;
        QString skill_name = room->askForChoice(xiahoushi, objectName(), skill_list.join("+"));
        xiahoushi->tag["XiaodeSkill"] = skill_name;
        room->acquireSkill(xiahoushi, skill_name);
        return false;
    }
};

class XiaodeEx: public TriggerSkill {
public:
    XiaodeEx(): TriggerSkill("#xiaode") {
        events << EventPhaseChanging << EventLoseSkill << Death;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                QString skill_name = player->tag["XiaodeSkill"].toString();
                if (!skill_name.isEmpty()) {
                    room->detachSkillFromPlayer(player, skill_name, false, true);
                    player->tag.remove("XiaodeSkill");
                }
            }
        } else if (triggerEvent == EventLoseSkill && data.toString() == "xiaode") {
            QString skill_name = player->tag["XiaodeSkill"].toString();
            if (!skill_name.isEmpty()) {
                room->detachSkillFromPlayer(player, skill_name, false, true);
                player->tag.remove("XiaodeSkill");
            }
        } else if (triggerEvent == Death && TriggerSkill::triggerable(player)) {
            DeathStruct death = data.value<DeathStruct>();
            QStringList skill_list;
            skill_list.append(addSkillList(death.who->getGeneral()));
            skill_list.append(addSkillList(death.who->getGeneral2()));
            player->tag["XiaodeVictimSkills"] = QVariant::fromValue(skill_list);
        }
        return false;
    }

private:
    QStringList addSkillList(const General *general) const{
        if (!general) return QStringList();
        QStringList skill_list;
        foreach (const Skill *skill, general->getSkillList()) {
            if (skill->isVisible() && !skill->isLordSkill() && skill->getFrequency() != Skill::Wake)
                skill_list.append(skill->objectName());
        }
        return skill_list;
    }
};

ZhoufuCard::ZhoufuCard() {
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhoufuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    return targets.isEmpty() && to_select != Self && to_select->getPile("incantation").isEmpty();
}

void ZhoufuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    ServerPlayer *target = targets.first();
    target->tag["ZhoufuSource" + QString::number(getEffectiveId())] = QVariant::fromValue(source);
    room->broadcastSkillInvoke("zhoufu");
    target->addToPile("incantation", this);
}

class ZhoufuViewAsSkill: public OneCardViewAsSkill {
public:
    ZhoufuViewAsSkill(): OneCardViewAsSkill("zhoufu") {
        filter_pattern = ".|.|.|hand";
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return !player->hasUsed("ZhoufuCard");
    }

    virtual const Card *viewAs(const Card *originalcard) const{
        Card *card = new ZhoufuCard;
        card->addSubcard(originalcard);
        return card;
    }
};

class Zhoufu: public TriggerSkill {
public:
    Zhoufu(): TriggerSkill("zhoufu") {
        events << StartJudge << EventPhaseChanging;
        view_as_skill = new ZhoufuViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL && target->getPile("incantation").length() > 0;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == StartJudge) {
            int card_id = player->getPile("incantation").first();

            JudgeStruct *judge = data.value<JudgeStruct *>();
            judge->card = Sanguosha->getCard(card_id);

            LogMessage log;
            log.type = "$ZhoufuJudge";
            log.from = player;
            log.arg = objectName();
            log.card_str = QString::number(judge->card->getEffectiveId());
            room->sendLog(log);

            room->moveCardTo(judge->card, NULL, judge->who, Player::PlaceJudge,
                             CardMoveReason(CardMoveReason::S_REASON_JUDGE,
                             judge->who->objectName(),
                             QString(), QString(), judge->reason), true);
            judge->updateResult();
            room->setTag("SkipGameRule", true);
        } else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                int id = player->getPile("incantation").first();
                ServerPlayer *zhangbao = player->tag["ZhoufuSource" + QString::number(id)].value<ServerPlayer *>();
                if (zhangbao && zhangbao->isAlive())
                    zhangbao->obtainCard(Sanguosha->getCard(id));
            }
        }
        return false;
    }
};

class Yingbing: public TriggerSkill {
public:
    Yingbing(): TriggerSkill("yingbing") {
        events << StartJudge;
        frequency = Frequent;
    }

    virtual int getPriority(TriggerEvent) const{
        return -1;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        JudgeStruct *judge = data.value<JudgeStruct *>();
        int id = judge->card->getEffectiveId();
        ServerPlayer *zhangbao = player->tag["ZhoufuSource" + QString::number(id)].value<ServerPlayer *>();
        if (zhangbao && TriggerSkill::triggerable(zhangbao)
            && zhangbao->askForSkillInvoke(objectName(), data)) {
                room->broadcastSkillInvoke(objectName());
                zhangbao->drawCards(2, "yingbing");
        }
        return false;
    }
};

class Kangkai: public TriggerSkill {
public:
    Kangkai(): TriggerSkill("kangkai") {
        events << TargetConfirmed;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            foreach (ServerPlayer *to, use.to) {
                if (!player->isAlive()) break;
                if (player->distanceTo(to) <= 1 && TriggerSkill::triggerable(player)) {
                    player->tag["KangkaiSlash"] = data;
                    bool will_use = room->askForSkillInvoke(player, objectName(), QVariant::fromValue(to));
                    player->tag.remove("KangkaiSlash");
                    if (!will_use) continue;

                    player->drawCards(1, "kangkai");

                    room->broadcastSkillInvoke(objectName());

                    if (!player->isNude() && player != to) {
                        const Card *card = NULL;
                        if (player->getCardCount() > 1) {
                            card = room->askForCard(player, "..!", "@kangkai-give:" + to->objectName(), data, Card::MethodNone);
                            if (!card)
                                card = player->getCards("he").at(qrand() % player->getCardCount());
                        } else {
                            Q_ASSERT(player->getCardCount() == 1);
                            card = player->getCards("he").first();
                        }
                        to->obtainCard(card);
                        if (card->getTypeId() == Card::TypeEquip && room->getCardOwner(card->getEffectiveId()) == to
                            && !to->isLocked(card)) {
                            to->tag["KangkaiSlash"] = data;
                            to->tag["KangkaiGivenCard"] = QVariant::fromValue(card);
                            bool will_use = room->askForSkillInvoke(to, "kangkai_use", "use");
                            to->tag.remove("KangkaiSlash");
                            to->tag.remove("KangkaiGivenCard");
                            if (will_use)
                                room->useCard(CardUseStruct(card, to, to));
                        }
                    }
                }
            }
        }
        return false;
    }
};

class Shenxian: public TriggerSkill {
public:
    Shenxian(): TriggerSkill("shenxian") {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player->getPhase() == Player::NotActive && move.from && move.from->isAlive()
            && move.from->objectName() != player->objectName()
            && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
            foreach (int id, move.card_ids) {
                if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic) {
                    if (room->askForSkillInvoke(player, objectName(), data)) {
                        room->broadcastSkillInvoke(objectName());
                        player->drawCards(1, "shenxian");
                    }
                    break;
                }
            }
        }
        return false;
    }
};

QiangwuCard::QiangwuCard() {
    target_fixed = true;
}

void QiangwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const{
    JudgeStruct judge;
    judge.pattern = ".";
    judge.who = source;
    judge.reason = "qiangwu";
    judge.play_animation = false;
    room->judge(judge);

    bool ok = false;
    int num = judge.pattern.toInt(&ok);
    if (ok)
        room->setPlayerMark(source, "qiangwu", num);
}

class QiangwuViewAsSkill: public ZeroCardViewAsSkill {
public:
    QiangwuViewAsSkill(): ZeroCardViewAsSkill("qiangwu") {
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return !player->hasUsed("QiangwuCard");
    }

    virtual const Card *viewAs() const{
        return new QiangwuCard;
    }
};

class Qiangwu: public TriggerSkill {
public:
    Qiangwu(): TriggerSkill("qiangwu") {
        events << EventPhaseChanging << PreCardUsed << FinishJudge;
        view_as_skill = new QiangwuViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                room->setPlayerMark(player, "qiangwu", 0);
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == "qiangwu")
                judge->pattern = QString::number(judge->card->getNumber());
        } else if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && player->getMark("qiangwu") > 0
                && use.card->getNumber() > player->getMark("qiangwu")) {
                if (use.m_addHistory) {
                    room->addPlayerHistory(player, use.card->getClassName(), -1);
                    use.m_addHistory = false;
                    data = QVariant::fromValue(use);
                }
            }
        }
        return false;
    }
};

class QiangwuTargetMod: public TargetModSkill {
public:
    QiangwuTargetMod(): TargetModSkill("#qiangwu-target") {
    }

    virtual int getDistanceLimit(const Player *from, const Card *card) const{
        if (card->getNumber() < from->getMark("qiangwu"))
            return 1000;
        else
            return 0;
    }

    virtual int getResidueNum(const Player *from, const Card *card) const{
        if (from->getMark("qiangwu") > 0
            && (card->getNumber() > from->getMark("qiangwu") || card->hasFlag("Global_SlashAvailabilityChecker")))
            return 1000;
        else
            return 0;
    }
};

#include "jsonutils.h"
class AocaiViewAsSkill: public ZeroCardViewAsSkill {
public:
    AocaiViewAsSkill(): ZeroCardViewAsSkill("aocai") {
    }

    virtual bool isEnabledAtPlay(const Player *) const{
        return false;
    }

    virtual bool isEnabledAtResponse(const Player *player, const QString &pattern) const{
        if (player->getPhase() != Player::NotActive || player->hasFlag("Global_AocaiFailed")) return false;
        if (pattern == "slash")
            return Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        else if (pattern == "peach")
            return player->getMark("Global_PreventPeach") == 0;
        else if (pattern.contains("analeptic"))
            return true;
        return false;
    }

    virtual const Card *viewAs() const{
        AocaiCard *aocai_card = new AocaiCard;
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (pattern == "peach+analeptic" && Self->getMark("Global_PreventPeach") > 0)
            pattern = "analeptic";
        aocai_card->setUserString(pattern);
        return aocai_card;
    }
};

class Aocai: public TriggerSkill {
public:
    Aocai(): TriggerSkill("aocai") {
        events << CardAsked;
        view_as_skill = new AocaiViewAsSkill;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        QString pattern = data.toStringList().first();
        if (player->getPhase() == Player::NotActive
            && (pattern == "slash" || pattern == "jink")
            && room->askForSkillInvoke(player, objectName(), data)) {
            QList<int> ids = room->getNCards(2, false);
            QList<int> enabled, disabled;
            foreach (int id, ids) {
                if (Sanguosha->getCard(id)->objectName().contains(pattern))
                    enabled << id;
                else
                    disabled << id;
            }
            int id = Aocai::view(room, player, ids, enabled, disabled);
            if (id != -1) {
                const Card *card = Sanguosha->getCard(id);
                room->provide(card);
                return true;
            }
        }
        return false;
    }

    static int view(Room *room, ServerPlayer *player, QList<int> &ids, QList<int> &enabled, QList<int> &disabled) {
        int result = -1, index = -1;
        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = player;
        log.card_str = IntList2StringList(ids).join("+");
        room->sendLog(log, player);

        room->broadcastSkillInvoke("aocai");
        room->notifySkillInvoked(player, "aocai");
        if (enabled.isEmpty()) {
            Json::Value arg(Json::arrayValue);
            arg[0] = QSanProtocol::Utils::toJsonString(".");
            arg[1] = false;
            arg[2] = QSanProtocol::Utils::toJsonArray(ids);
            room->doNotify(player, QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, arg);
        } else {
            room->fillAG(ids, player, disabled);
            int id = room->askForAG(player, enabled, true, "aocai");
            if (id != -1) {
                index = ids.indexOf(id);
                ids.removeOne(id);
                result = id;
            }
            room->clearAG(player);
        }

        QList<int> &drawPile = room->getDrawPile();
        for (int i = ids.length() - 1; i >= 0; --i)
            drawPile.prepend(ids.at(i));
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_UPDATE_PILE, Json::Value(drawPile.length()));
        if (result == -1)
            room->setPlayerFlag(player, "Global_AocaiFailed");
        else {
            LogMessage log;
            log.type = "#AocaiUse";
            log.from = player;
            log.arg = "aocai";
            log.arg2 = QString("CAPITAL(%1)").arg(index + 1);
            room->sendLog(log);
        }
        return result;
    }
};

YinbingCard::YinbingCard() {
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void YinbingCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const{
    source->addToPile("yinbing", this);
}

class YinbingViewAsSkill: public ViewAsSkill {
public:
    YinbingViewAsSkill(): ViewAsSkill("yinbing") {
        response_pattern = "@@yinbing";
    }

    virtual bool viewFilter(const QList<const Card *> &, const Card *to_select) const{
        return to_select->getTypeId() != Card::TypeBasic;
    }

    virtual const Card *viewAs(const QList<const Card *> &cards) const{
        if (cards.length() == 0) return NULL;

        Card *acard = new YinbingCard;
        acard->addSubcards(cards);
        acard->setSkillName(objectName());
        return acard;
    }
};

class Yinbing: public TriggerSkill {
public:
    Yinbing(): TriggerSkill("yinbing") {
        events << EventPhaseStart << Damaged;
        view_as_skill = new YinbingViewAsSkill;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish && !player->isNude()) {
            room->askForUseCard(player, "@@yinbing", "@yinbing", -1, Card::MethodNone);
        } else if (triggerEvent == Damaged && !player->getPile("yinbing").isEmpty()) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))) {
                room->notifySkillInvoked(player, objectName());
                LogMessage log;
                log.type = "#TriggerSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);

                QList<int> ids = player->getPile("yinbing");
                room->fillAG(ids, player);
                int id = room->askForAG(player, ids, false, objectName());
                room->clearAG(player);
                room->throwCard(id, NULL);
            }
        }

        return false;
    }
};

class Juedi: public PhaseChangeSkill {
public:
    Juedi(): PhaseChangeSkill("juedi") {
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return PhaseChangeSkill::triggerable(target) && target->getPhase() == Player::Start
            && !target->getPile("yinbing").isEmpty();
    }

    virtual bool onPhaseChange(ServerPlayer *target) const{
        Room *room = target->getRoom();
        if (!room->askForSkillInvoke(target, objectName())) return false;
        room->broadcastSkillInvoke(objectName());

        QList<ServerPlayer *> playerlist;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (p->getHp() <= target->getHp())
                playerlist << p;
        }
        ServerPlayer *to_give = NULL;
        if (!playerlist.isEmpty())
            to_give = room->askForPlayerChosen(target, playerlist, objectName(), "@juedi", true);
        if (to_give) {
            room->recover(to_give, RecoverStruct(target));
            DummyCard *dummy = new DummyCard(target->getPile("yinbing"));
            room->obtainCard(to_give, dummy);
            delete dummy;
        } else {
            int len = target->getPile("yinbing").length();
            target->clearOnePrivatePile("yinbing");
            if (target->isAlive())
                room->drawCards(target, len, objectName());
        }
        return false;
    }
};

class Gongao: public TriggerSkill {
public:
    Gongao(): TriggerSkill("gongao") {
        events << Death;
        frequency = Compulsory;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const{
        LogMessage log;
        log.type = "#TriggerSkill";
        log.arg = objectName();
        log.from = player;
        room->sendLog(log);

        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());

        LogMessage log2;
        log2.type = "#GainMaxHp";
        log2.from = player;
        log2.arg = "1";
        room->sendLog(log2);

        room->setPlayerProperty(player, "maxhp", player->getMaxHp() + 1);

        if (player->isWounded()) {
            room->recover(player, RecoverStruct(player));
        } else {
            LogMessage log3;
            log3.type = "#GetHp";
            log3.from = player;
            log3.arg = QString::number(player->getHp());
            log3.arg2 = QString::number(player->getMaxHp());
            room->sendLog(log3);
        }
        return false;
    }
};

class Juyi: public PhaseChangeSkill {
public:
    Juyi(): PhaseChangeSkill("juyi") {
        frequency = Wake;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL && PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Start
            && target->getMark("juyi") == 0
            && target->isWounded()
            && target->getMaxHp() > target->aliveCount();
    }

    virtual bool onPhaseChange(ServerPlayer *zhugedan) const{
        Room *room = zhugedan->getRoom();

        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(zhugedan, objectName());
        room->doLightbox("$JuyiAnimate");

        LogMessage log;
        log.type = "#JuyiWake";
        log.from = zhugedan;
        log.arg = QString::number(zhugedan->getMaxHp());
        log.arg2 = QString::number(zhugedan->aliveCount());
        room->sendLog(log);

        room->setPlayerMark(zhugedan, "juyi", 1);
        room->addPlayerMark(zhugedan, "@waked");
        int diff = zhugedan->getHandcardNum() - zhugedan->getMaxHp();
        if (diff < 0)
            room->drawCards(zhugedan, -diff, objectName());
        if (zhugedan->getMark("juyi") == 1)
            room->handleAcquireDetachSkills(zhugedan, "benghuai|weizhong");

        return false;
    }
};

class Weizhong: public TriggerSkill {
public:
    Weizhong(): TriggerSkill("weizhong") {
        events << MaxHpChanged;
        frequency = Compulsory;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const{
        LogMessage log;
        log.type = "#TriggerSkill";
        log.arg = objectName();
        log.from = player;
        room->sendLog(log);

        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());

        player->drawCards(1, objectName());
        return false;
    }
};

XiemuCard::XiemuCard() {
    target_fixed = true;
}

void XiemuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const{
    QString kingdom = room->askForKingdom(source);
    room->setPlayerMark(source, "@xiemu_" + kingdom, 1);
}

class XiemuViewAsSkill: public OneCardViewAsSkill {
public:
    XiemuViewAsSkill(): OneCardViewAsSkill("xiemu") {
        filter_pattern = "Slash";
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return player->canDiscard(player, "he") && !player->hasUsed("XiemuCard");
    }

    virtual const Card *viewAs(const Card *originalCard) const{
        XiemuCard *card = new XiemuCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Xiemu: public TriggerSkill {
public:
    Xiemu(): TriggerSkill("xiemu") {
        events << TargetConfirmed << EventPhaseStart;
        view_as_skill = new XiemuViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && player != use.from && use.card->getTypeId() != Card::TypeSkill
                && use.card->isBlack() && use.to.contains(player)
                && player->getMark("@xiemu_" + use.from->getKingdom()) > 0) {
                    LogMessage log;
                    log.type = "#InvokeSkill";
                    log.from = player;
                    log.arg = objectName();
                    room->sendLog(log);

                    room->notifySkillInvoked(player, objectName());
                    player->drawCards(2, objectName());
            }
        } else {
            if (player->getPhase() == Player::RoundStart) {
                foreach (QString kingdom, Sanguosha->getKingdoms()) {
                    QString markname = "@xiemu_" + kingdom;
                    if (player->getMark(markname) > 0)
                        room->setPlayerMark(player, markname, 0);
                }
            }
        }
        return false;
    }
};

class Naman: public TriggerSkill {
public:
    Naman(): TriggerSkill("naman") {
        events << BeforeCardsMove;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile) return false;
        const Card *to_obtain = NULL;
        if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_RESPONSE) {
            if (move.from && player->objectName() == move.from->objectName())
                return false;
            to_obtain = move.reason.m_extraData.value<const Card *>();
            if (!to_obtain || !to_obtain->isKindOf("Slash"))
                return false;
        } else {
            return false;
        }
        if (to_obtain && room->askForSkillInvoke(player, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            room->obtainCard(player, to_obtain);
            move.removeCardIds(move.card_ids);
            data = QVariant::fromValue(move);
        }

        return false;
    }
};

AocaiCard::AocaiCard() {
}

bool AocaiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    const Card *card = NULL;
    if (!user_string.isEmpty())
        card = Sanguosha->cloneCard(user_string.split("+").first());
    return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
}

bool AocaiCard::targetFixed() const{
    if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
        return true;

    const Card *card = NULL;
    if (!user_string.isEmpty())
        card = Sanguosha->cloneCard(user_string.split("+").first());
    return card && card->targetFixed();
}

bool AocaiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const{
    const Card *card = NULL;
    if (!user_string.isEmpty())
        card = Sanguosha->cloneCard(user_string.split("+").first());
    return card && card->targetsFeasible(targets, Self);
}

const Card *AocaiCard::validateInResponse(ServerPlayer *user) const{
    Room *room = user->getRoom();
    QList<int> ids = room->getNCards(2, false);
    QStringList names = user_string.split("+");
    if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

    QList<int> enabled, disabled;
    foreach (int id, ids) {
        if (names.contains(Sanguosha->getCard(id)->objectName()))
            enabled << id;
        else
            disabled << id;
    }

    LogMessage log;
    log.type = "#InvokeSkill";
    log.from = user;
    log.arg = "aocai";
    room->sendLog(log);

    int id = Aocai::view(room, user, ids, enabled, disabled);
    return Sanguosha->getCard(id);
}

const Card *AocaiCard::validate(CardUseStruct &cardUse) const{
    cardUse.m_isOwnerUse = false;
    ServerPlayer *user = cardUse.from;
    Room *room = user->getRoom();
    QList<int> ids = room->getNCards(2, false);
    QStringList names = user_string.split("+");
    if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

    QList<int> enabled, disabled;
    foreach (int id, ids) {
        if (names.contains(Sanguosha->getCard(id)->objectName()))
            enabled << id;
        else
            disabled << id;
    }

    LogMessage log;
    log.type = "#InvokeSkill";
    log.from = user;
    log.arg = "aocai";
    room->sendLog(log);

    int id = Aocai::view(room, user, ids, enabled, disabled);
    return Sanguosha->getCard(id);
}

DuwuCard::DuwuCard() {
}

bool DuwuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (!targets.isEmpty() || qMax(0, to_select->getHp()) != subcardsLength())
        return false;

    if (Self->getWeapon() && subcards.contains(Self->getWeapon()->getId())) {
        const Weapon *weapon = qobject_cast<const Weapon *>(Self->getWeapon()->getRealCard());
        int distance_fix = weapon->getRange() - Self->getAttackRange(false);
        if (Self->getOffensiveHorse() && subcards.contains(Self->getOffensiveHorse()->getId()))
            distance_fix += 1;
        return Self->inMyAttackRange(to_select, distance_fix);
    } else if (Self->getOffensiveHorse() && subcards.contains(Self->getOffensiveHorse()->getId())) {
        return Self->inMyAttackRange(to_select, 1);
    } else
        return Self->inMyAttackRange(to_select);
}

void DuwuCard::onEffect(const CardEffectStruct &effect) const{
    effect.from->getRoom()->damage(DamageStruct("duwu", effect.from, effect.to));
}

class DuwuViewAsSkill: public ViewAsSkill {
public:
    DuwuViewAsSkill(): ViewAsSkill("duwu") {
    }

    virtual bool isEnabledAtPlay(const Player *player) const{
        return player->canDiscard(player, "he") && !player->hasFlag("DuwuEnterDying");
    }

    virtual bool viewFilter(const QList<const Card *> &, const Card *to_select) const{
        return !Self->isJilei(to_select);
    }

    virtual const Card *viewAs(const QList<const Card *> &cards) const{
        DuwuCard *duwu = new DuwuCard;
        if (!cards.isEmpty())
            duwu->addSubcards(cards);
        return duwu;
    }
};

class Duwu: public TriggerSkill {
public:
    Duwu(): TriggerSkill("duwu") {
        events << QuitDying;
        view_as_skill = new DuwuViewAsSkill;
    }

    virtual bool triggerable(const ServerPlayer *target) const{
        return target != NULL;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const{
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.damage && dying.damage->getReason() == "duwu" && !dying.damage->chain && !dying.damage->transfer) {
            ServerPlayer *from = dying.damage->from;
            if (from && from->isAlive()) {
                room->setPlayerFlag(from, "DuwuEnterDying");
                room->loseHp(from, 1);
            }
        }
        return false;
    }
};

class Dujin: public DrawCardsSkill {
public:
    Dujin(): DrawCardsSkill("dujin") {
        frequency = Frequent;
    }

    virtual int getDrawNum(ServerPlayer *player, int n) const{
        if (player->askForSkillInvoke(objectName())) {
            player->getRoom()->broadcastSkillInvoke(objectName());
            return n + player->getEquips().length() / 2 + 1;
        } else
            return n;
    }
};

QingyiCard::QingyiCard() {
    mute = true;
}

bool QingyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    Slash *slash = new Slash(NoSuit, 0);
    slash->setSkillName("qingyi");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

void QingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    foreach (ServerPlayer *target, targets) {
        if (!source->canSlash(target, NULL, false))
            targets.removeOne(target);
    }

    if (targets.length() > 0) {
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_qingyi");
        room->useCard(CardUseStruct(slash, source, targets));
    }
}

class QingyiViewAsSkill: public ZeroCardViewAsSkill {
public:
    QingyiViewAsSkill(): ZeroCardViewAsSkill("qingyi") {
        response_pattern = "@@qingyi";
    }

    virtual bool isEnabledAtPlay(const Player *) const{
        return false;
    }

    virtual bool isEnabledAtResponse(const Player *, const QString &pattern) const{
        return pattern == "@@qingyi";
    }

    virtual const Card *viewAs() const{
        return new QingyiCard;
    }
};

class Qingyi: public TriggerSkill {
public:
    Qingyi(): TriggerSkill("qingyi") {
        events << EventPhaseChanging;
        view_as_skill = new QingyiViewAsSkill;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::Judge && !player->isSkipped(Player::Judge)
            && !player->isSkipped(Player::Draw)) {
            if (Slash::IsAvailable(player) && room->askForUseCard(player, "@@qingyi", "@qingyi-slash")) {
                player->skip(Player::Judge, true);
                player->skip(Player::Draw, true);
            }
        }
        return false;
    }
};

class Shixin: public TriggerSkill {
public:
    Shixin(): TriggerSkill("shixin") {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    virtual bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire) {
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#ShixinProtect";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = "fire_nature";
            room->sendLog(log);
            return true;
        }
        return false;
    }
};

SPCardPackage::SPCardPackage()
    : Package("sp_cards")
{
    (new SPMoonSpear)->setParent(this);
    skills << new SPMoonSpearSkill;

    type = CardPack;
}

ADD_PACKAGE(SPCard)

SPPackage::SPPackage()
    : Package("sp")
{
    General *yangxiu = new General(this, "yangxiu", "wei", 3); // SP 001
    yangxiu->addSkill(new Jilei);
    yangxiu->addSkill(new JileiClear);
    yangxiu->addSkill(new Danlao);
    related_skills.insertMulti("jilei", "#jilei-clear");

    General *sp_diaochan = new General(this, "sp_diaochan", "qun", 3, false, true); // SP 002
    sp_diaochan->addSkill("noslijian");
    sp_diaochan->addSkill("biyue");

    General *gongsunzan = new General(this, "gongsunzan", "qun"); // SP 003
    gongsunzan->addSkill(new Yicong);
    gongsunzan->addSkill(new YicongEffect);
    related_skills.insertMulti("yicong", "#yicong-effect");

    General *yuanshu = new General(this, "yuanshu", "qun"); // SP 004
    yuanshu->addSkill(new Yongsi);
    yuanshu->addSkill(new Weidi);

    General *sp_sunshangxiang = new General(this, "sp_sunshangxiang", "shu", 3, false, true); // SP 005
    sp_sunshangxiang->addSkill("jieyin");
    sp_sunshangxiang->addSkill("xiaoji");

    General *sp_pangde = new General(this, "sp_pangde", "wei", 4, true, true); // SP 006
    sp_pangde->addSkill("mashu");
    sp_pangde->addSkill("mengjin");

    General *sp_guanyu = new General(this, "sp_guanyu", "wei", 4); // SP 007
    sp_guanyu->addSkill("wusheng");
    sp_guanyu->addSkill(new Danji);

    General *shenlvbu1 = new General(this, "shenlvbu1", "god", 8, true, true); // SP 008 (2-1)
    shenlvbu1->addSkill("mashu");
    shenlvbu1->addSkill("wushuang");

    General *shenlvbu2 = new General(this, "shenlvbu2", "god", 4, true, true); // SP 008 (2-2)
    shenlvbu2->addSkill("mashu");
    shenlvbu2->addSkill("wushuang");
    shenlvbu2->addSkill(new Xiuluo);
    shenlvbu2->addSkill(new ShenweiKeep);
    shenlvbu2->addSkill(new Shenwei);
    shenlvbu2->addSkill(new Shenji);
    related_skills.insertMulti("shenwei", "#shenwei-draw");

    General *sp_caiwenji = new General(this, "sp_caiwenji", "wei", 3, false, true); // SP 009
    sp_caiwenji->addSkill("beige");
    sp_caiwenji->addSkill("duanchang");

    General *sp_machao = new General(this, "sp_machao", "qun", 4, true, true); // SP 011
    sp_machao->addSkill("mashu");
    sp_machao->addSkill("nostieji");

    General *sp_jiaxu = new General(this, "sp_jiaxu", "wei", 3, true, true); // SP 012
    sp_jiaxu->addSkill("wansha");
    sp_jiaxu->addSkill("luanwu");
    sp_jiaxu->addSkill("weimu");

    General *caohong = new General(this, "caohong", "wei"); // SP 013
    caohong->addSkill(new Yuanhu);

    General *guanyinping = new General(this, "guanyinping", "shu", 3, false); // SP 014
    guanyinping->addSkill(new Xueji);
    guanyinping->addSkill(new Huxiao);
    guanyinping->addSkill(new HuxiaoCount);
    guanyinping->addSkill(new HuxiaoClear);
    guanyinping->addSkill(new Wuji);
    guanyinping->addSkill(new WujiCount);
    related_skills.insertMulti("wuji", "#wuji-count");
    related_skills.insertMulti("huxiao", "#huxiao-count");
    related_skills.insertMulti("huxiao", "#huxiao-clear");

    General *sp_zhenji = new General(this, "sp_zhenji", "wei", 3, false, true); // SP 015
    sp_zhenji->addSkill("qingguo");
    sp_zhenji->addSkill("luoshen");

    General *xiahouba = new General(this, "xiahouba", "shu"); // SP 019
    xiahouba->addSkill(new Baobian);

    General *chenlin = new General(this, "chenlin", "wei", 3); // SP 020
    chenlin->addSkill(new Bifa);
    chenlin->addSkill(new Songci);

    General *erqiao = new General(this, "erqiao", "wu", 3, false); // SP 021
    erqiao->addSkill(new Xingwu);
    erqiao->addSkill(new Luoyan);

    General *sp_shenlvbu = new General(this, "sp_shenlvbu", "god", 5, true, true); // SP 022
    sp_shenlvbu->addSkill("kuangbao");
    sp_shenlvbu->addSkill("wumou");
    sp_shenlvbu->addSkill("wuqian");
    sp_shenlvbu->addSkill("shenfen");

    General *xiahoushi = new General(this, "xiahoushi", "shu", 3, false); // SP 023
    xiahoushi->addSkill(new Yanyu);
    xiahoushi->addSkill(new Xiaode);
    xiahoushi->addSkill(new XiaodeEx);
    related_skills.insertMulti("xiaode", "#xiaode");

    General *sp_yuejin = new General(this, "sp_yuejin", "wei", 4, true, true); // SP 024
    sp_yuejin->addSkill("xiaoguo");

    General *zhangbao = new General(this, "zhangbao", "qun", 3); // SP 025
    zhangbao->addSkill(new Zhoufu);
    zhangbao->addSkill(new Yingbing);

    General *caoang = new General(this, "caoang", "wei"); // SP 026
    caoang->addSkill(new Kangkai);

    General *sp_zhugejin = new General(this, "sp_zhugejin", "wu", 3, true, true); // SP 027
    sp_zhugejin->addSkill("hongyuan");
    sp_zhugejin->addSkill("huanshi");
    sp_zhugejin->addSkill("mingzhe");

    General *xingcai = new General(this, "xingcai", "shu", 3, false); // SP 028
    xingcai->addSkill(new Shenxian);
    xingcai->addSkill(new Qiangwu);
    xingcai->addSkill(new QiangwuTargetMod);
    related_skills.insertMulti("qiangwu", "#qiangwu-target");

    General *sp_panfeng = new General(this, "sp_panfeng", "qun", 4, true, true); // SP 029
    sp_panfeng->addSkill("kuangfu");

    General *zumao = new General(this, "zumao", "wu"); // SP 030
    zumao->addSkill(new Yinbing);
    zumao->addSkill(new Juedi);

    General *sp_dingfeng = new General(this, "sp_dingfeng", "wu", 4, true, true); // SP 031
    sp_dingfeng->addSkill("duanbing");
    sp_dingfeng->addSkill("fenxun");

    General *zhugedan = new General(this, "zhugedan", "wei", 4); // SP 032
    zhugedan->addSkill(new Gongao);
    zhugedan->addSkill(new Juyi);
    zhugedan->addRelateSkill("weizhong");

    General *sp_hetaihou = new General(this, "sp_hetaihou", "qun", 3, false, true); // SP 033
    sp_hetaihou->addSkill("zhendu");
    sp_hetaihou->addSkill("qiluan");

    General *maliang = new General(this, "maliang", "shu", 3); // SP 035
    maliang->addSkill(new Xiemu);
    maliang->addSkill(new Naman);

    addMetaObject<YuanhuCard>();
    addMetaObject<XuejiCard>();
    addMetaObject<BifaCard>();
    addMetaObject<SongciCard>();
    addMetaObject<ZhoufuCard>();
    addMetaObject<QiangwuCard>();
    addMetaObject<YinbingCard>();
    addMetaObject<XiemuCard>();

    skills << new Weizhong;
}

ADD_PACKAGE(SP)

OLPackage::OLPackage()
    : Package("OL")
{
    General *zhugeke = new General(this, "zhugeke", "wu", 3); // OL 002
    zhugeke->addSkill(new Aocai);
    zhugeke->addSkill(new Duwu);

    General *lingcao = new General(this, "lingcao", "wu", 4);
    lingcao->addSkill(new Dujin);

    General *sunru = new General(this, "sunru", "wu", 3, false);
    sunru->addSkill(new Qingyi);
    sunru->addSkill(new SlashNoDistanceLimitSkill("qingyi"));
    sunru->addSkill(new Shixin);
    related_skills.insertMulti("qingyi", "#qingyi-slash-ndl");

    General *ol_fazheng = new General(this, "ol_fazheng", "shu", 3, true, true);
    ol_fazheng->addSkill("enyuan");
    ol_fazheng->addSkill("xuanhuo");

    General *ol_xushu = new General(this, "ol_xushu", "shu", 3, true, true);
    ol_xushu->addSkill("wuyan");
    ol_xushu->addSkill("jujian");

    General *ol_guanxingzhangbao = new General(this, "ol_guanxingzhangbao", "shu", 4, true, true);
    ol_guanxingzhangbao->addSkill("fuhun");

    General *ol_madai = new General(this, "ol_madai", "shu", 4, true, true);
    ol_madai->addSkill("mashu");
    ol_madai->addSkill("qianxi");

    General *ol_wangyi = new General(this, "ol_wangyi", "wei", 3, false, true);
    ol_wangyi->addSkill("zhenlie");
    ol_wangyi->addSkill("miji");

    addMetaObject<AocaiCard>();
    addMetaObject<DuwuCard>();
    addMetaObject<QingyiCard>();
}

ADD_PACKAGE(OL)

TaiwanSPPackage::TaiwanSPPackage()
    : Package("Taiwan_sp")
{
    General *tw_caocao = new General(this, "tw_caocao$", "wei", 4, true, true); // TW SP 019
    tw_caocao->addSkill("nosjianxiong");
    tw_caocao->addSkill("hujia");

    General *tw_simayi = new General(this, "tw_simayi", "wei", 3, true, true);
    tw_simayi->addSkill("nosfankui");
    tw_simayi->addSkill("nosguicai");

    General *tw_xiahoudun = new General(this, "tw_xiahoudun", "wei", 4, true, true); // TW SP 025
    tw_xiahoudun->addSkill("nosganglie");

    General *tw_zhangliao = new General(this, "tw_zhangliao", "wei", 4, true, true); // TW SP 013
    tw_zhangliao->addSkill("nostuxi");

    General *tw_xuchu = new General(this, "tw_xuchu", "wei", 4, true, true);
    tw_xuchu->addSkill("nosluoyi");

    General *tw_guojia = new General(this, "tw_guojia", "wei", 3, true, true); // TW SP 015
    tw_guojia->addSkill("tiandu");
    tw_guojia->addSkill("nosyiji");

    General *tw_zhenji = new General(this, "tw_zhenji", "wei", 3, false, true); // TW SP 007
    tw_zhenji->addSkill("qingguo");
    tw_zhenji->addSkill("luoshen");

    General *tw_liubei = new General(this, "tw_liubei$", "shu", 4, true, true); // TW SP 017
    tw_liubei->addSkill("rende");
    tw_liubei->addSkill("jijiang");

    General *tw_guanyu = new General(this, "tw_guanyu", "shu", 4, true, true); // TW SP 018
    tw_guanyu->addSkill("wusheng");

    General *tw_zhangfei = new General(this, "tw_zhangfei", "shu", 4, true, true);
    tw_zhangfei->addSkill("paoxiao");

    General *tw_zhugeliang = new General(this, "tw_zhugeliang", "shu", 3, true, true); // TW SP 012
    tw_zhugeliang->addSkill("guanxing");
    tw_zhugeliang->addSkill("kongcheng");

    General *tw_zhaoyun = new General(this, "tw_zhaoyun", "shu", 4, true, true); // TW SP 006
    tw_zhaoyun->addSkill("longdan");

    General *tw_machao = new General(this, "tw_machao", "shu", 4, true, true); // TW SP 010
    tw_machao->addSkill("mashu");
    tw_machao->addSkill("nostieji");

    General *tw_huangyueying = new General(this, "tw_huangyueying", "shu", 3, false, true); // TW SP 011
    tw_huangyueying->addSkill("nosjizhi");
    tw_huangyueying->addSkill("nosqicai");

    General *tw_sunquan = new General(this, "tw_sunquan$", "wu", 4, true, true); // TW SP 021
    tw_sunquan->addSkill("zhiheng");
    tw_sunquan->addSkill("jiuyuan");

    General *tw_ganning = new General(this, "tw_ganning", "wu", 4, true, true); // TW SP 009
    tw_ganning->addSkill("qixi");

    General *tw_lvmeng = new General(this, "tw_lvmeng", "wu", 4, true, true);
    tw_lvmeng->addSkill("keji");

    General *tw_huanggai = new General(this, "tw_huanggai", "wu", 4, true, true); // TW SP 014
    tw_huanggai->addSkill("noskurou");

    General *tw_zhouyu = new General(this, "tw_zhouyu", "wu", 3, true, true);
    tw_zhouyu->addSkill("nosyingzi");
    tw_zhouyu->addSkill("nosfanjian");

    General *tw_daqiao = new General(this, "tw_daqiao", "wu", 3, false, true); // TW SP 005
    tw_daqiao->addSkill("nosguose");
    tw_daqiao->addSkill("liuli");

    General *tw_luxun = new General(this, "tw_luxun", "wu", 3, true, true); // TW SP 016
    tw_luxun->addSkill("nosqianxun");
    tw_luxun->addSkill("noslianying");

    General *tw_sunshangxiang = new General(this, "tw_sunshangxiang", "wu", 3, false, true); // TW SP 028
    tw_sunshangxiang->addSkill("jieyin");
    tw_sunshangxiang->addSkill("xiaoji");

    /*General *tw_huatuo = new General(this, "tw_huatuo", 3, true, true);
    tw_huatuo->addSkill("qingnang");
    tw_huatuo->addSkill("jijiu");*/

    General *tw_lvbu = new General(this, "tw_lvbu", "qun", 4, true, true); // TW SP 008
    tw_lvbu->addSkill("wushuang");

    General *tw_diaochan = new General(this, "tw_diaochan", "qun", 3, false, true); // TW SP 002
    tw_diaochan->addSkill("noslijian");
    tw_diaochan->addSkill("biyue");

    General *tw_xiaoqiao = new General(this, "tw_xiaoqiao", "wu", 3, false, true);
    tw_xiaoqiao->addSkill("tianxiang");
    tw_xiaoqiao->addSkill("hongyan");

    General *tw_yuanshu = new General(this, "tw_yuanshu", "qun", 4, true, true); // TW SP 004
    tw_yuanshu->addSkill("yongsi");
    tw_yuanshu->addSkill("weidi");
}

ADD_PACKAGE(TaiwanSP)

MiscellaneousPackage::MiscellaneousPackage()
    : Package("miscellaneous")
{
    General *wz_daqiao = new General(this, "wz_daqiao", "wu", 3, false, true); // WZ 001
    wz_daqiao->addSkill("nosguose");
    wz_daqiao->addSkill("liuli");

    General *wz_xiaoqiao = new General(this, "wz_xiaoqiao", "wu", 3, false, true); // WZ 002
    wz_xiaoqiao->addSkill("tianxiang");
    wz_xiaoqiao->addSkill("hongyan");

    General *pr_shencaocao = new General(this, "pr_shencaocao", "god", 3, true, true); // PR LE 005
    pr_shencaocao->addSkill("guixin");
    pr_shencaocao->addSkill("feiying");

    General *pr_nos_simayi = new General(this, "pr_nos_simayi", "wei", 3, true, true); // PR WEI 002
    pr_nos_simayi->addSkill("nosfankui");
    pr_nos_simayi->addSkill("nosguicai");
}

ADD_PACKAGE(Miscellaneous)

HegemonySPPackage::HegemonySPPackage()
    : Package("hegemony_sp")
{
    General *sp_heg_zhouyu = new General(this, "sp_heg_zhouyu", "wu", 3, true, true); // GSP 001
    sp_heg_zhouyu->addSkill("nosyingzi");
    sp_heg_zhouyu->addSkill("nosfanjian");

    General *sp_heg_xiaoqiao = new General(this, "sp_heg_xiaoqiao", "wu", 3, false, true); // GSP 002
    sp_heg_xiaoqiao->addSkill("tianxiang");
    sp_heg_xiaoqiao->addSkill("hongyan");
}

ADD_PACKAGE(HegemonySP)
