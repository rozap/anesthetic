mod util;
use std::fs::File;
use std::io::Write;
use std::path::Path;
use std::{env, usize};
use std::{error::Error, io};
use termion::input::TermRead;
use termion::{event::Key, input::MouseTerminal, raw::IntoRawMode, screen::AlternateScreen};
use tui::backend::Backend;
use tui::layout::{Corner, Rect};
use tui::{Frame, symbols};
use tui::widgets::{Axis, Chart, Dataset, GraphType, List, ListItem};
use tui::{
    backend::TermionBackend,
    layout::{Constraint, Direction, Layout},
    style::{Color, Modifier, Style},
    text::{Span, Spans},
    widgets::{Block, Borders, Gauge, Tabs},
    Terminal,
};
use util::event::{Event, Events};
use util::TabsState;
// use std::{thread, time};

struct BsodApp<'a> {
    tabs: TabsState<'a>,
}

struct SensorConstants {
    oil_temp_max: u16,
    oil_pressure_max: u16,
    coolant_pressure_max: u16,
    voltage_max: u16,
    rpm_max: u16,
}


const WINDOW_SIZE: usize = 50;
const SENSOR_CONSTANTS: SensorConstants = SensorConstants {
    oil_pressure_max: 80,
    oil_temp_max: 260,
    coolant_pressure_max: 80,
    voltage_max: 16 * 1000,
    rpm_max: 7000,
};


fn gauge<'a>(title: String, label: String, value: u16, max: u16) -> Gauge<'a> {
    let percent = ((value as f32) / (max as f32)) * 100.0;
    Gauge::default()
        .block(Block::default().title(title).borders(Borders::ALL))
        .gauge_style(Style::default().fg(Color::LightCyan).bg(Color::DarkGray))
        .percent(percent.round().min(100.0) as u16)
        .label(label)
}

fn chart<'a>(area: Rect, title: String, x_title: &'a str, y_title: &'a str, series: &'a Vec<Vec<(f64, f64)>>, colors: Vec<Color>, x_labels: Vec<&'a str>, min: u16, max: u16, step: usize) -> Chart<'a> {
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

struct Window {
    buf: Vec<u16>,
    size: usize,
}

impl Window {
    fn new(size: usize) -> Window {
        Window { size: size, buf: vec![0; size] }
    }

    fn last(&self) -> u16 {
        self.buf[0]
    }

    fn put(&mut self, value: &str) -> Result<(), String> {
        match value.parse::<u16>() {
            Ok(v) => {
                if self.buf.len() >= self.size {
                    self.buf.pop();
                }
                self.buf.insert(0, v);
                Ok(())
            }
            Err(e) => Err(format!("Failed to parse as uint: {}", e)),
        }
    }

    fn last_n(&self, n: usize) -> Vec<(f64, u16)> {
        self.buf[0..n.min(self.buf.len())].iter().rev().enumerate().map(|(i, v)| {
            (i as f64, v.to_owned())
        }).collect()
    }
}
enum LogLevel {
    Info,
    Warn,
    Error
}

struct Connection {
    oil_temps: Window,
    oil_pressures: Window,
    coolant_pressures: Window,
    voltages: Window,
    rpms: Window,
    rssis: Window,
    file: File,

    counter: u128,
    events: Vec<(u128, String, LogLevel)>
}


impl Connection {
    fn init(filename: &Path) -> Result<Connection, io::Error> {
        Ok(Connection {
            file: File::open(filename)?,
            oil_temps: Window::new(WINDOW_SIZE),
            oil_pressures: Window::new(WINDOW_SIZE),
            coolant_pressures: Window::new(WINDOW_SIZE),
            rpms: Window::new(WINDOW_SIZE),
            rssis: Window::new(WINDOW_SIZE),
            voltages: Window::new(WINDOW_SIZE),
            counter: 0,
            events: vec![
                (0, "w e l c o m e  t o  l e m o n s  d o m i n a t i o n".to_owned(), LogLevel::Info)
            ]
        })
    }

    pub fn log(&mut self, lvl: LogLevel, msg: String) {
        self.events.push((self.counter, msg, lvl));
    }

    pub fn read(&mut self) {
        let result = match self.file.read_line() {
            Ok(Some(line)) => {
                self.counter = self.counter + 1;


                match line
                    .split_ascii_whitespace()
                    .collect::<Vec<&str>>()
                    .as_slice()
                {
                    [_month, _day, _time, keyvalue] => {
                        match keyvalue.split(":").collect::<Vec<&str>>().as_slice() {
                            ["P_C", value] => {
                                self.coolant_pressures.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["P_O", value] => {
                                self.oil_pressures.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["T_O", value] => {
                                self.oil_temps.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["VBA", value] => {
                                self.voltages.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["RPM", value] => {
                                self.rpms.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["RSI", value] => {
                                self.rssis.put("0");
                                Some((LogLevel::Info, line))
                            }
                            ["FLT", value] => {
                                Some((LogLevel::Warn, format!("Fault: {}", value)))
                            }
                            _ => Some((LogLevel::Error, format!("Invalid key value pair {}", keyvalue))),
                        }
                    }

                    _ => {
                        if line.is_empty() {
                            None
                        } else {
                            Some((LogLevel::Error, format!("Invalid data line '{}'", line)))
                        }
                    },
                }
            }
            Ok(None) => Some((LogLevel::Error, format!("Empty line?"))),
            Err(e) => Some((LogLevel::Error, format!("Failed to read line: {}", e))),
        };



        match result {
            Some((level, line)) => {
                self.log(level, line.clone());
            }
            None => {
                // noop
            }

        };

    }
}


fn to_volt(v: u16) -> f64 {
    (v as f64) / 1000.0
}
fn render_gauges<'a, B>(frame: &'a mut Frame<B>, area: Rect, connection: &Connection)
where B: Backend {
    let gauge_chunks = Layout::default()
        .direction(Direction::Vertical)
        .margin(0)
        .constraints(
            [
                Constraint::Percentage(20),
                Constraint::Percentage(20),
                Constraint::Percentage(20),
                Constraint::Percentage(20),
                Constraint::Percentage(20),
            ]
            .as_ref(),
        )
        .split(area);

    let oil_temp = connection.oil_temps.last();
    let oil_temp_gauge = gauge(
        format!("Oil Temperature"),
        format!("{} F", oil_temp),
        oil_temp,
        SENSOR_CONSTANTS.oil_temp_max,
    );

    let oil_pressure = connection.oil_pressures.last();
    let oil_pressure_gauge = gauge(
        format!("Oil Pressure"),
        format!("{} PSI", oil_pressure),
        oil_pressure,
        SENSOR_CONSTANTS.oil_pressure_max,
    );

    let coolant_pressure = connection.coolant_pressures.last();
    let coolant_pressure_gauge = gauge(
        format!("Coolant Pressure"),
        format!("{} PSI", coolant_pressure),
        coolant_pressure,
        SENSOR_CONSTANTS.coolant_pressure_max,
    );

    let voltage = connection.voltages.last();
    let voltage_gauge = gauge(
        format!("Volts"),
        format!("{:.2} volts", to_volt(voltage)),
        voltage,
        SENSOR_CONSTANTS.voltage_max,
    );

    let rpm = connection.rpms.last();
    let rpm_gauge = gauge(
        format!("Max RPM"),
        format!("{} rpm", rpm),
        rpm,
        SENSOR_CONSTANTS.rpm_max,
    );

    frame.render_widget(oil_temp_gauge, gauge_chunks[0]);
    frame.render_widget(oil_pressure_gauge, gauge_chunks[1]);
    frame.render_widget(coolant_pressure_gauge, gauge_chunks[2]);
    frame.render_widget(voltage_gauge, gauge_chunks[3]);
    frame.render_widget(rpm_gauge, gauge_chunks[4]);
}


const DEFAULT_BG: Color = Color::Black;

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();
    let program = args[0].clone();
    let usage = format!("Usage: {} FILE [options]", program);

    let filename = match &args[..] {
        [_, file] => {
            // let wtf = String::from(f);
            Path::new(&file).to_owned()
        },
        _ => {
            println!("{}", usage);
            return Err("No file provided".into())
        }
    };

    let mut connection = match Connection::init(&filename) {
        Ok(c) => c,
        Err(e) => {
            return Err("Failed to open a connection".into())
        },
    };


    // App

    let stdout = io::stdout().into_raw_mode()?;
    let stdout = MouseTerminal::from(stdout);
    let stdout = AlternateScreen::from(stdout);
    let backend = TermionBackend::new(stdout);

    let mut terminal = Terminal::new(backend)?;
    let events = Events::new();
    let mut app = BsodApp {
        // add tabs here
        tabs: TabsState::new(vec!["Main", "Temperatures", "Pressures", "Voltages", "RPMs"]),
    };


    // Main loop
    loop {
        connection.read();


        let res = terminal.draw(|f| {
            let size = f.size();
            let chunks = Layout::default()
                .direction(Direction::Vertical)
                .margin(5)
                .constraints([Constraint::Length(3), Constraint::Min(0)].as_ref())
                .split(size);

            let block = Block::default().style(Style::default().bg(DEFAULT_BG).fg(Color::Gray));
            f.render_widget(block, size);
            let titles = app
                .tabs
                .titles
                .iter()
                .map(|t| {
                    let (first, rest) = t.split_at(1);
                    Spans::from(vec![
                        Span::styled(first, Style::default().fg(Color::LightYellow)),
                        Span::styled(rest, Style::default().fg(Color::Green)),
                    ])
                })
                .collect();
            let tabs = Tabs::new(titles)
                .block(Block::default().borders(Borders::ALL).title("Tabs"))
                .select(app.tabs.index)
                .style(Style::default().fg(Color::Cyan).bg(DEFAULT_BG))
                .highlight_style(
                    Style::default()
                        .add_modifier(Modifier::BOLD)
                        .fg(Color::Black)
                        .bg(Color::LightGreen),
                );
            f.render_widget(tabs, chunks[0]);
            let body = chunks[1];
            let block = Block::default().borders(Borders::ALL).title("Graphs");

            let main_chunks = Layout::default()
                .direction(Direction::Vertical)
                .margin(2)
                .constraints(
                    [Constraint::Percentage(50), Constraint::Percentage(50)].as_ref(),
                )
                .split(body);

            f.render_widget(block, body);


            let events: Vec<ListItem> = connection.events
                .iter()
                .rev()
                .filter(|(_counter, _event, level)| {
                    match level {
                        &LogLevel::Error => true,
                        &LogLevel::Warn => true,
                        &LogLevel::Info => false
                    }
                })
                .map(|(counter, event, level)| {
                    // Colorcode the level depending on its type
                    let s = match level {
                        LogLevel::Error => Style::default().fg(Color::Red),
                        LogLevel::Warn => Style::default().fg(Color::Yellow),
                        LogLevel::Info => Style::default().fg(Color::Blue),
                    };

                    let level_label = match level {
                        LogLevel::Error => "Error",
                        LogLevel::Warn => "Warn",
                        LogLevel::Info => "Info",
                    };
                    let header = Spans::from(vec![
                        Span::styled(format!("{:<9}", level_label), s),
                        Span::raw(" "),
                        Span::styled(
                            format!("{}", counter),
                            Style::default().add_modifier(Modifier::ITALIC),
                        ),
                    ]);
                    // The event gets it's own line
                    let log = Spans::from(vec![Span::raw(event)]);

                    // Here several things happen:
                    // 1. Add a `---` spacing line above the final list entry
                    // 3. Add a spacer line
                    // 4. Add the actual event
                    ListItem::new(vec![
                        Spans::from("-".repeat(chunks[1].width as usize)),
                        header,
                        log,
                    ])
                })
                .collect();
            let events_list = List::new(events)
                .block(Block::default().borders(Borders::ALL).title(format!("Event Log [received {} messages]", connection.counter)))
                .start_corner(Corner::BottomLeft);
            f.render_widget(events_list, main_chunks[1]);


            match app.tabs.index {
                0 => {
                    render_gauges(f, main_chunks[0], &connection)
                }
                1 => {
                    let data = vec![
                        connection.oil_temps.last_n(512).iter().map(|(i, t)| (i.to_owned(), t.to_owned() as f64)).collect()
                    ];

                    let c = chart(
                        main_chunks[0],
                        format!("Temperatures"),
                        "Time",
                        "Temperature (F)",
                        &data,
                        vec![Color::Cyan],
                        vec!["Oil Temp"],
                        100,
                        SENSOR_CONSTANTS.oil_temp_max,
                        40
                    );

                    f.render_widget(c, main_chunks[0])
                }
                2 => {
                    let data = vec![
                        connection.oil_pressures.last_n(512).iter().map(|(i, t)| (i.to_owned(), t.to_owned() as f64)).collect(),
                        connection.coolant_pressures.last_n(512).iter().map(|(i, t)| (i.to_owned(), t.to_owned() as f64)).collect()
                    ];

                    let c = chart(
                        main_chunks[0],
                        format!("Pressures"),
                        "Time",
                        "Pressure (PSI)",
                        &data,
                        vec![Color::Cyan, Color::Red],
                        vec![
                            "Oil Pressure",
                            "Coolant Pressure"
                        ],
                        0,
                        SENSOR_CONSTANTS.oil_pressure_max.max(SENSOR_CONSTANTS.coolant_pressure_max),
                        20
                    );

                    f.render_widget(c, main_chunks[0])
                },
                3 => {
                    let data = vec![
                        connection.voltages.last_n(512).iter().map(|(i, v)| (i.to_owned(), to_volt(v.to_owned()))).collect(),
                    ];

                    let c = chart(
                        main_chunks[0],
                        format!("Voltages"),
                        "Time",
                        "Volts (V)",
                        &data,
                        vec![Color::Green],
                        vec![
                            "Voltage"
                        ],
                        10,
                        to_volt(SENSOR_CONSTANTS.voltage_max).ceil() as u16,
                        1
                    );

                    f.render_widget(c, main_chunks[0])
                },
                4 => {
                    let data = vec![
                        connection.rpms.last_n(512).iter().map(|(i, t)| (i.to_owned(), t.to_owned() as f64)).collect()
                    ];

                    let c = chart(
                        main_chunks[0],
                        format!("RPM"),
                        "Time",
                        "RPM",
                        &data,
                        vec![Color::Cyan],
                        vec!["RPM"],
                        1000,
                        SENSOR_CONSTANTS.rpm_max,
                        1000
                    );

                    f.render_widget(c, main_chunks[0])
                }
                _ => panic!("invalid tab?"),
            };
        })?;


        if let Event::Input(input) = events.next()? {
            match input {
                Key::Char('q') => {
                    break;
                }
                Key::Right => app.tabs.next(),
                Key::Left => app.tabs.previous(),
                Key::Char('m') => app.tabs.goto(0),
                Key::Char('t') => app.tabs.goto(1),
                Key::Char('p') => app.tabs.goto(2),
                Key::Char('v') => app.tabs.goto(3),
                Key::Char('r') => app.tabs.goto(4),
                _ => {}
            }
        }
    }
    Ok(())
}
