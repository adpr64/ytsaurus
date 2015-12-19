#pragma once

#include "private.h"
#include "group.h"
#include "user.h"

#include <yt/server/cell_master/bootstrap.h>
#include <yt/server/cell_master/public.h>

#include <yt/server/object_server/object_detail.h>
#include <yt/server/object_server/public.h>

#include <yt/server/security_server/security_manager.h>

#include <yt/core/ytree/convert.h>
#include <yt/core/ytree/fluent.h>

namespace NYT {
namespace NSecurityServer {

////////////////////////////////////////////////////////////////////////////////

template <class TImpl>
class TSubjectProxy
    : public NObjectServer::TNonversionedObjectProxyBase<TImpl>
{
public:
    TSubjectProxy(NCellMaster::TBootstrap* bootstrap, TImpl* subject)
        : TBase(bootstrap, subject)
    { }

private:
    typedef NObjectServer::TNonversionedObjectProxyBase<TImpl> TBase;

protected:
    virtual void ListSystemAttributes(std::vector<NYTree::ISystemAttributeProvider::TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        descriptors->push_back(NYTree::ISystemAttributeProvider::TAttributeDescriptor("name")
            .SetReplicated(true)
            .SetMandatory(true));
        descriptors->push_back("member_of");
        descriptors->push_back("member_of_closure");
    }

    virtual bool GetBuiltinAttribute(const Stroka& key, NYson::IYsonConsumer* consumer) override
    {
        const auto* subject = this->GetThisTypedImpl();

        if (key == "name") {
            NYTree::BuildYsonFluently(consumer)
                .Value(subject->GetName());
            return true;
        }

        if (key == "member_of") {
            NYTree::BuildYsonFluently(consumer)
                .DoListFor(subject->MemberOf(), [] (NYTree::TFluentList fluent, TGroup* group) {
                    fluent
                        .Item().Value(group->GetName());
                });
            return true;
        }

        if (key == "member_of_closure") {
            NYTree::BuildYsonFluently(consumer)
                .DoListFor(subject->RecursiveMemberOf(), [] (NYTree::TFluentList fluent, TGroup* group) {
                    fluent
                        .Item().Value(group->GetName());
                });
            return true;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }

    virtual bool SetBuiltinAttribute(const Stroka& key, const NYson::TYsonString& value) override
    {
        auto* subject = this->GetThisTypedImpl();
        auto securityManager = this->Bootstrap_->GetSecurityManager();

        if (key == "name") {
            auto newName = NYTree::ConvertTo<Stroka>(value);
            securityManager->RenameSubject(subject, newName);
            return true;
        }

        return TBase::SetBuiltinAttribute(key, value);
    }

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NSecurityServer
} // namespace NYT

