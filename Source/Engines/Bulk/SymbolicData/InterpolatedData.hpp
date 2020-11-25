#pragma once

#include "SymbolicData.hpp"

namespace boss::engines::bulk {

template <typename TableMaterializedView, typename T, typename RefT>
class InterpolatedData : public MissingData<T> {
public:
  InterpolatedData(std::string const& column, std::string const& refColumn)
      : MissingData<T>(), m_column(column), m_refColumn(refColumn) {}

  bool Evaluate(void const* view, size_t rowIndex) override {
    TableMaterializedView const& materializedView =
        *reinterpret_cast<TableMaterializedView const*>(view);

    // query from db previous and next values (closest defined ones)

    auto const& series = materializedView.project({m_refColumn, m_column});
    auto const& filtered = series.symbolicFilter(m_column, false);

    auto const& timeSeries = series.project({m_refColumn});
    auto const& filteredTimeSeries = filtered.project({m_refColumn});

    RefT currentTime = timeSeries.template get<RefT>(0, rowIndex);
    RefT prevTime = filteredTimeSeries
                        .select(m_refColumn, currentTime, TableMaterializedView::PredicateOp::LESS)
                        .aggregate(m_refColumn, TableMaterializedView::AggregateOp::MAX)
                        .template get<RefT>(0, 0);
    RefT nextTime =
        filteredTimeSeries
            .select(m_refColumn, currentTime, TableMaterializedView::PredicateOp::GREATER)
            .aggregate(m_refColumn, TableMaterializedView::AggregateOp::MIN)
            .template get<RefT>(0, 0);

    auto const& prevCell =
        filtered.select(m_refColumn, prevTime, TableMaterializedView::PredicateOp::EQUALS)
            .project({m_column});
    auto const& nextCell =
        filtered.select(m_refColumn, nextTime, TableMaterializedView::PredicateOp::EQUALS)
            .project({m_column});

    T prevValue = prevCell.template get<T>(0, 0);
    T nextValue = nextCell.template get<T>(0, 0);

    // simple interpolation
    float timeInterval = static_cast<float>(nextTime - prevTime);
    float ratioToNext =
        (timeInterval < 0.001f) ? 0.5f : static_cast<float>(currentTime - prevTime) / timeInterval;
    float currentValue =
        static_cast<float>(prevValue) + ratioToNext * static_cast<float>(nextValue - prevValue);

    this->m_value = static_cast<T>(currentValue);

    this->m_missingValue = false;
    return true;
  }

private:
  std::string m_column;
  std::string m_refColumn;
};

} // namespace boss::engines::bulk
