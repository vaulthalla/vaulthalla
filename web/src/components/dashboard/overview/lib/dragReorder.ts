import { visibleDashboardLayoutCards, type DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'

export function reorderLayoutBefore(layout: DashboardLayoutCard[], dragId: string, targetId: string): DashboardLayoutCard[] {
  if (dragId === targetId) return layout

  const visible = visibleDashboardLayoutCards(layout)
  const from = visible.findIndex(card => card.instanceId === dragId)
  const to = visible.findIndex(card => card.instanceId === targetId)
  if (from < 0 || to < 0) return layout

  const reordered = [...visible]
  const [moved] = reordered.splice(from, 1)
  reordered.splice(to, 0, moved)
  const orderByInstanceId = new Map(reordered.map((card, order) => [card.instanceId, order]))
  return layout.map(card => orderByInstanceId.has(card.instanceId) ? { ...card, order: orderByInstanceId.get(card.instanceId) ?? card.order } : card)
}
