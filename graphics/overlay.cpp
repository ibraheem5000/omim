#include "overlay.hpp"
#include "overlay_renderer.hpp"

#include "../base/logging.hpp"
#include "../base/stl_add.hpp"

#include "../std/bind.hpp"
#include "../std/vector.hpp"


namespace graphics
{
  bool betterOverlayElement(shared_ptr<OverlayElement> const & l,
                            shared_ptr<OverlayElement> const & r)
  {
    // "frozen" object shouldn't be popped out.
    if (r->isFrozen())
      return false;

    // for the composite elements, collected in OverlayRenderer to replace the part elements
    return l->priority() > r->priority();
  }

  m2::RectD const OverlayElementTraits::LimitRect(shared_ptr<OverlayElement> const & elem)
  {
    return elem->roughBoundRect();
  }

  void DrawIfNotCancelled(OverlayRenderer * r,
                          shared_ptr<OverlayElement> const & e,
                          math::Matrix<double, 3, 3> const & m)
  {
    if (!r->isCancelled())
      e->draw(r, m);
  }

  void Overlay::draw(OverlayRenderer * r, math::Matrix<double, 3, 3> const & m)
  {
    m_tree.ForEach(bind(&DrawIfNotCancelled, r, _1, cref(m)));
  }

  Overlay::Overlay()
    : m_couldOverlap(true)
  {}

  void Overlay::setCouldOverlap(bool flag)
  {
    m_couldOverlap = flag;
  }

  template <typename Tree>
  void offsetTree(Tree & tree, m2::PointD const & offs, m2::RectD const & r)
  {
    m2::AnyRectD AnyRect(r);
    typedef typename Tree::elem_t elem_t;
    vector<elem_t> elems;
    tree.ForEach(MakeBackInsertFunctor(elems));
    tree.Clear();

    for (typename vector<elem_t>::iterator it = elems.begin(); it != elems.end(); ++it)
    {
      (*it)->offset(offs);
      vector<m2::AnyRectD> const & aaLimitRects = (*it)->boundRects();
      bool doAppend = false;

      (*it)->setIsNeedRedraw(false);
      (*it)->setIsFrozen(true);

      bool hasInside = false;
      bool hasOutside = false;

      for (int i = 0; i < aaLimitRects.size(); ++i)
      {
        bool isPartInsideRect = AnyRect.IsRectInside(aaLimitRects[i]);

        if (isPartInsideRect)
        {
          if (hasOutside)
          {
            /// intersecting
            (*it)->setIsNeedRedraw(true);
            doAppend = true;
            break;
          }
          else
          {
            hasInside = true;
            doAppend = true;
            continue;
          }
        }

        bool isRectInsidePart = aaLimitRects[i].IsRectInside(AnyRect);

        if (isRectInsidePart)
        {
          doAppend = true;
          break;
        }

        bool isPartIntersectRect = AnyRect.IsIntersect(aaLimitRects[i]);

        if (isPartIntersectRect)
        {
          /// intersecting
          (*it)->setIsNeedRedraw(true);
          doAppend = true;
          break;
        }

        /// the last case remains here - part rect is outside big rect
        if (hasInside)
        {
          (*it)->setIsNeedRedraw(true);
          doAppend = true;
          break;
        }
        else
          hasOutside = true;
      }

      if (doAppend)
        tree.Add(*it);
    }
  }

  void Overlay::offset(m2::PointD const & offs, m2::RectD const & rect)
  {
    offsetTree(m_tree, offs, rect);
  }

  size_t Overlay::getElementsCount() const
  {
    return m_tree.GetSize();
  }

  void Overlay::lock()
  {
    m_mutex.Lock();
  }

  void Overlay::unlock()
  {
    m_mutex.Unlock();
  }

  void Overlay::clear()
  {
    m_tree.Clear();
  }

  void Overlay::addOverlayElement(shared_ptr<OverlayElement> const & oe)
  {
    m_tree.Add(oe);
  }

  struct DoPreciseSelectByPoint
  {
    m2::PointD m_pt;
    list<shared_ptr<OverlayElement> > * m_elements;

    DoPreciseSelectByPoint(m2::PointD const & pt, list<shared_ptr<OverlayElement> > * elements)
      : m_pt(pt), m_elements(elements)
    {}

    void operator()(shared_ptr<OverlayElement> const & e)
    {
      if (e->hitTest(m_pt))
        m_elements->push_back(e);
    }
  };

  struct DoPreciseSelectByRect
  {
    m2::AnyRectD m_rect;
    list<shared_ptr<OverlayElement> > * m_elements;

    DoPreciseSelectByRect(m2::RectD const & rect,
                          list<shared_ptr<OverlayElement> > * elements)
      : m_rect(rect),
        m_elements(elements)
    {}

    void operator()(shared_ptr<OverlayElement> const & e)
    {
      vector<m2::AnyRectD> const & rects = e->boundRects();

      for (vector<m2::AnyRectD>::const_iterator it = rects.begin();
           it != rects.end();
           ++it)
      {
        m2::AnyRectD const & rect = *it;

        if (m_rect.IsIntersect(rect))
        {
          m_elements->push_back(e);
          break;
        }
      }
    }
  };

  struct DoPreciseIntersect
  {
    shared_ptr<OverlayElement> m_oe;
    bool * m_isIntersect;

    DoPreciseIntersect(shared_ptr<OverlayElement> const & oe, bool * isIntersect)
      : m_oe(oe),
        m_isIntersect(isIntersect)
    {}

    void operator()(shared_ptr<OverlayElement> const & e)
    {
      if (*m_isIntersect)
        return;

      if (m_oe->m_userInfo == e->m_userInfo)
        return;

      vector<m2::AnyRectD> const & lr = m_oe->boundRects();
      vector<m2::AnyRectD> const & rr = e->boundRects();

      for (vector<m2::AnyRectD>::const_iterator lit = lr.begin(); lit != lr.end(); ++lit)
      {
        for (vector<m2::AnyRectD>::const_iterator rit = rr.begin(); rit != rr.end(); ++rit)
        {
          *m_isIntersect = lit->IsIntersect(*rit);
          if (*m_isIntersect)
            return;
        }
      }
    }
  };

  void Overlay::selectOverlayElements(m2::RectD const & rect, list<shared_ptr<OverlayElement> > & res) const
  {
    DoPreciseSelectByRect fn(rect, &res);
    m_tree.ForEachInRect(rect, fn);
  }

  void Overlay::replaceOverlayElement(shared_ptr<OverlayElement> const & oe)
  {
    bool isIntersect = false;
    DoPreciseIntersect fn(oe, &isIntersect);
    m_tree.ForEachInRect(oe->roughBoundRect(), fn);
    if (isIntersect)
      m_tree.ReplaceIf(oe, &betterOverlayElement);
    else
      m_tree.Add(oe);
  }

  void Overlay::removeOverlayElement(shared_ptr<OverlayElement> const & oe, m2::RectD const & r)
  {
    m_tree.Erase(oe, r);
  }

  void Overlay::processOverlayElement(shared_ptr<OverlayElement> const & oe, math::Matrix<double, 3, 3> const & m)
  {
    oe->setTransformation(m);
    if (oe->isValid())
      processOverlayElement(oe);
  }

  void Overlay::processOverlayElement(shared_ptr<OverlayElement> const & oe)
  {
    if (oe->isValid())
    {
      if (m_couldOverlap)
        addOverlayElement(oe);
      else
        replaceOverlayElement(oe);
    }
  }

  bool greater_priority(shared_ptr<OverlayElement> const & l,
                        shared_ptr<OverlayElement> const & r)
  {
    return l->priority() > r->priority();
  }

  void Overlay::merge(Overlay const & layer, math::Matrix<double, 3, 3> const & m)
  {
    vector<shared_ptr<OverlayElement> > v;

    /// 1. collecting all elements from tree
    layer.m_tree.ForEach(MakeBackInsertFunctor(v));

    /// 2. sorting by priority, so the more important ones comes first
    sort(v.begin(), v.end(), &greater_priority);

    /// 3. merging them into the infoLayer starting from most
    /// important one to optimize the space usage.
    // @TODO Avoid std::bind overload compile error in the better way
    void (Overlay::*fn)(shared_ptr<OverlayElement> const &,
                        math::Matrix<double, 3, 3> const &) = &Overlay::processOverlayElement;
    for_each(v.begin(), v.end(), bind(fn, this, _1, cref(m)));
  }

  void Overlay::merge(Overlay const & infoLayer)
  {
    vector<shared_ptr<OverlayElement> > v;

    /// 1. collecting all elements from tree
    infoLayer.m_tree.ForEach(MakeBackInsertFunctor(v));

    /// 2. sorting by priority, so the more important ones comes first
    sort(v.begin(), v.end(), &greater_priority);

    /// 3. merging them into the infoLayer starting from most
    /// important one to optimize the space usage.
    // @TODO Avoid std::bind overload compile error in the better way
    void (Overlay::*fn)(shared_ptr<OverlayElement> const &) = &Overlay::processOverlayElement;
    for_each(v.begin(), v.end(), bind(fn, this, _1));
  }

  void Overlay::clip(m2::RectI const & r)
  {
    vector<shared_ptr<OverlayElement> > v;
    m_tree.ForEach(MakeBackInsertFunctor(v));
    m_tree.Clear();

    //int clippedCnt = 0;
    //int elemCnt = v.size();

    m2::RectD rd(r);
    m2::AnyRectD ard(rd);

    for (unsigned i = 0; i < v.size(); ++i)
    {
      shared_ptr<OverlayElement> const & e = v[i];

      if (!e->isVisible())
      {
        //clippedCnt++;
        continue;
      }

      if (e->roughBoundRect().IsIntersect(rd))
      {
        bool hasIntersection = false;
        for (unsigned j = 0; j < e->boundRects().size(); ++j)
        {
          if (ard.IsIntersect(e->boundRects()[j]))
          {
            hasIntersection = true;
            break;
          }
        }

        if (hasIntersection)
          processOverlayElement(e);
      }
      //else
      //  clippedCnt++;
    }

//    LOG(LINFO, ("clipped out", clippedCnt, "elements,", elemCnt, "elements total"));
  }
}
