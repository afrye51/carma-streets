/**
 * Intersection Model API
 * No description provided (generated by Openapi Generator https://github.com/openapitools/openapi-generator)
 *
 * The version of the OpenAPI document: 1.0.0
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */

#ifndef OAI_OAIDefaultApiHandler_H
#define OAI_OAIDefaultApiHandler_H

#include <QObject>

#include "OAIIntersection_info.h"
#include "OAILanelet_info.h"
#include <QString>
#include "intersection_model.h"

namespace OpenAPI {

class OAIDefaultApiHandler : public QObject
{
    Q_OBJECT

public:
    OAIDefaultApiHandler();
    virtual ~OAIDefaultApiHandler();
    std::shared_ptr<intersection_model::intersection_model> int_worker ;

public slots:
    virtual void getConflictLanelets(qint32 link_lanelet_id);
    virtual void getIntersectionInfo();
    virtual void listDepartureLanelets();
    virtual void listEntryLanelets();
    virtual void listLinkLanelets();
    

};

}

#endif // OAI_OAIDefaultApiHandler_H