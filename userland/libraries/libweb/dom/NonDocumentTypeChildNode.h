/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/Forward.h>
#include <libweb/Forward.h>
#include <libweb/TreeNode.h>

namespace Web::DOM {

template<typename NodeType>
class NonDocumentTypeChildNode {
public:
    Element* previous_element_sibling()
    {
        for (auto* sibling = static_cast<NodeType*>(this)->previous_sibling(); sibling; sibling = sibling->previous_sibling()) {
            if (is<Element>(*sibling))
                return verify_cast<Element>(sibling);
        }
        return nullptr;
    }

    Element* next_element_sibling()
    {
        for (auto* sibling = static_cast<NodeType*>(this)->next_sibling(); sibling; sibling = sibling->next_sibling()) {
            if (is<Element>(*sibling))
                return verify_cast<Element>(sibling);
        }
        return nullptr;
    }

    Element* next_element_in_pre_order()
    {
        for (auto* node = static_cast<NodeType*>(this)->next_in_pre_order(); node; node = node->next_in_pre_order()) {
            if (is<Element>(*node))
                return verify_cast<Element>(node);
        }
        return nullptr;
    }

    const Element* previous_element_sibling() const { return const_cast<NonDocumentTypeChildNode*>(this)->previous_element_sibling(); }
    const Element* next_element_sibling() const { return const_cast<NonDocumentTypeChildNode*>(this)->next_element_sibling(); }
    const Element* next_element_in_pre_order() const { return const_cast<NonDocumentTypeChildNode*>(this)->next_element_in_pre_order(); }

protected:
    NonDocumentTypeChildNode() { }
};

}