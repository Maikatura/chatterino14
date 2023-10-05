#include "BttvBadges.hpp"

#include "common/NetworkRequest.hpp"
#include "common/NetworkResult.hpp"
#include "common/Outcome.hpp"
#include "messages/Emote.hpp"
#include "providers/ffz/FfzUtil.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QThread>
#include <QUrl>

#include <map>
#include <shared_mutex>

namespace chatterino {

void BttvBadges::initialize(Settings &settings, Paths &paths)
{
    this->load();
}

std::vector<BttvBadges::Badge> BttvBadges::getUserBadges(const UserId &id)
{
    std::vector<Badge> badges;

    std::shared_lock lock(this->mutex_);

    auto it = this->userBadges.find(id.string);
    if (it != this->userBadges.end())
    {
        for (const auto &badgeID : it->second)
        {
            if (auto badge = this->getBadge(badgeID); badge)
            {
                badges.emplace_back(*badge);
            }
        }
    }

    return badges;
}

boost::optional<BttvBadges::Badge> BttvBadges::getBadge(const int badgeID)
{
    auto it = this->badges.find(badgeID);
    if (it != this->badges.end())
    {
        return it->second;
    }

    return boost::none;
}

void BttvBadges::load()
{
    static QUrl url("https://api.betterttv.net/3/cached/badges/Twitch");

    NetworkRequest(url)
        .onSuccess([this](auto result) -> Outcome {
            std::unique_lock lock(this->mutex_);

            auto jsonRoot = result.parseJson();
            for (const auto &jsonBadge_ : jsonRoot.value("badges").toArray())
            {
                auto jsonBadge = jsonBadge_.toObject();
                auto jsonUrls = jsonBadge.value("urls").toObject();

                auto emote =
                    Emote{EmoteName{},
                          ImageSet{parseFfzUrl(jsonUrls.value("1").toString()),
                                   parseFfzUrl(jsonUrls.value("2").toString()),
                                   parseFfzUrl(jsonUrls.value("4").toString())},
                          Tooltip{jsonBadge.value("title").toString()}, Url{}};

                Badge badge;

                int badgeID = jsonBadge.value("id").toInt();

                this->badges[badgeID] = Badge{
                    std::make_shared<const Emote>(std::move(emote)),
                    QColor(jsonBadge.value("color").toString()),
                };

                // Find users with this badge
                auto badgeIDString = QString::number(badgeID);
                for (const auto &user : jsonRoot.value("users")
                                            .toObject()
                                            .value(badgeIDString)
                                            .toArray())
                {
                    auto userIDString = QString::number(user.toInt());

                    auto [userBadges, created] = this->userBadges.emplace(
                        std::make_pair<QString, std::set<int>>(
                            std::move(userIDString), {badgeID}));
                    if (!created)
                    {
                        // User already had a badge assigned
                        userBadges->second.emplace(badgeID);
                    }
                }
            }

            return Success;
        })
        .execute();
}

}  // namespace chatterino
