use tui::layout::{Rect};
use tui::style::{Color, Modifier};
use tui::{symbols};
use tui::widgets::{Axis, Chart, Dataset, GraphType};
use tui::{
    style::{Style},
    text::{Span},
    widgets::{Block, Borders, Gauge},
};

use crate::constants::DEFAULT_BG;

pub fn gauge<'a>(title: String, label: String, value: i16, max: i16) -> Gauge<'a> {
  let percent = ((value as f32) / (max as f32)) * 100.0;
  Gauge::default()
      .block(Block::default().title(title).borders(Borders::ALL))
      .gauge_style(Style::default().fg(Color::LightCyan).bg(Color::DarkGray))
      .percent(percent.round().min(100.0) as u16)
      .label(label)
}

pub fn chart<'a>(area: Rect, title: String, x_title: &'a str, y_title: &'a str, series: &'a Vec<Vec<(f64, f64)>>, colors: Vec<Color>, x_labels: Vec<&'a str>, min: i16, max: i16, step: usize) -> Chart<'a> {
  let max_len = series.iter().fold(0, |acc, s| acc.max(s.len()));
  let attributes: Vec<(&Color, &str)> = colors.iter().zip(x_labels).collect();
  let datasets = series.iter().zip(attributes).map(|(s, (color, label))| {
      Dataset::default()
      .name(label)
      .graph_type(GraphType::Line)
      .marker(symbols::Marker::Dot)
      .style(Style::default().bg(DEFAULT_BG).fg(color.to_owned()))
      .data(s)
  }).collect();

  let ticks = (min..max + 1).step_by(step).map(|num| {
      Span::styled(format!("{}", num), Style::default().add_modifier(Modifier::BOLD))
  }).collect();

  Chart::new(datasets)
      .block(
          Block::default()
              .title(Span::styled(
                  title,
                  Style::default()
                      .fg(Color::Cyan)
                      .add_modifier(Modifier::BOLD),
              ))
              .borders(Borders::ALL),
      )
      .x_axis(
          Axis::default()
              .title(x_title)
              .style(Style::default().fg(Color::Gray))
              .bounds([0.0, max_len as f64]),
      )
      .y_axis(
          Axis::default()
              .title(y_title)
              .style(Style::default().fg(Color::Gray))
              .labels(ticks)
              .bounds([min as f64, max as f64]),
      )
}
