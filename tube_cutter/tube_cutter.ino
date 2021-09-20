#include <AccelStepper.h>
#include <U8glib.h>
#include <Rotary.h>
#include <avdweb_Switch.h>
#include <FrequencyTimer2.h>
#include <EEPROM.h>

// Ramps pins ---------------------------------------------------
#define X_STEP_PIN 54
#define X_DIR_PIN 55
#define X_ENABLE_PIN 38

#define Y_STEP_PIN 60
#define Y_DIR_PIN 61
#define Y_ENABLE_PIN 56

#define Z_STEP_PIN 46
#define Z_DIR_PIN 48
#define Z_ENABLE_PIN 62

#define E_STEP_PIN 26
#define E_DIR_PIN 28
#define E_ENABLE_PIN 24

#define X_END_STOP 3
#define Y_END_STOP 14

#define A_ENC_PIN 31
#define B_ENC_PIN 33
#define BUTTON_PIN 35
#define BUZZER_PIN 37

#define FAN 9
//---------------------------------------------------------------

// Motors -------------------------------------------------------
// Tube driving wheel
AccelStepper drive(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
// Tube Locking wheel
AccelStepper lock(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);
// Cutting knife
AccelStepper knife(AccelStepper::DRIVER, Z_STEP_PIN, Z_DIR_PIN);
// Knife unlock
AccelStepper unlock(AccelStepper::DRIVER, E_STEP_PIN, E_DIR_PIN);
// Drill drive

// Display ------------------------------------------------------
U8GLIB_ST7920_128X64_1X u8g(23, 17, 16);
// --------------------------------------------------------------

// Encoder ------------------------------------------------------
Rotary encoder = Rotary(A_ENC_PIN, B_ENC_PIN);
// --------------------------------------------------------------

// Buttons ------------------------------------------------------
Switch ok_button(BUTTON_PIN), x_end_stop(X_END_STOP, INPUT, HIGH), y_end_stop(Y_END_STOP, INPUT, HIGH);
// --------------------------------------------------------------

const uint16_t MOTOR_REV = 6400, UNLOCK_STEPS = 1800;
const uint16_t MOTOR_SPEED = 17000, KNIFE_SPEED = 25000, MOTOR_SLOW_SPEED = 500;
const uint16_t MAGIC = 281;

const uint8_t MENU_CNT = 5;
const char *menu_elements[MENU_CNT] = {"PG:", "LN:", "AM:", "PD:", "RUN"};
const uint8_t NAMES_CNT = 7;
const char *program_names[] = {"KIT2 mala", "KIT2 velka", "Basic 2 mala", "Basic 2 velka", "Multipro mala", "Multipro velka", "Multipro 3D"};
int8_t menu_current = 0;

int8_t program_num = 1;
int16_t tube_des = 0;
int16_t tube_cnt = 0;
float tube_len = 0;
uint32_t drive_steps = 0;

bool up = false, down = false;
bool ok_button_state = false;
bool encoder_faster = false;

uint32_t start = 0, finished = 0;

// LOGO ---------------------------------------------------------
const uint8_t LOGO_HEIGHT = 32, LOGO_WIDTH = 4;
const uint8_t logo_bitmap[] U8G_PROGMEM = {
    0x00, 0x00, 0x00, 0x00, // ................................
    0x00, 0x1F, 0x80, 0x00, // ...........######...............
    0x00, 0xFF, 0xF0, 0x00, // ........############............
    0x03, 0xFF, 0xFC, 0x00, // ......################..........
    0x07, 0xFF, 0xFE, 0x00, // .....##################.........
    0x0F, 0xFF, 0xFF, 0x00, // ....####################........
    0x1F, 0xFF, 0xFF, 0x80, // ...######################.......
    0x3F, 0xFF, 0xFF, 0xC0, // ..########################......
    0x3F, 0xFF, 0xFF, 0xE0, // ..#########################.....
    0x3F, 0xFF, 0xFF, 0xE0, // ..#########################.....
    0x3F, 0xFF, 0xFF, 0xF0, // ..##########################....
    0x3F, 0xFF, 0xFF, 0xF0, // ..##########################....
    0x3F, 0xFF, 0xFF, 0xF0, // ..##########################....
    0x3F, 0xFF, 0xFF, 0xF8, // ..###########################...
    0x1F, 0xFF, 0xFF, 0xF8, // ...##########################...
    0x1F, 0xFF, 0xFF, 0xF8, // ...##########################...
    0x3F, 0xFF, 0xFF, 0xF8, // ..###########################...
    0x7F, 0xFF, 0xFF, 0xF8, // .############################...
    0x7F, 0xFF, 0xFF, 0xF8, // .############################...
    0xFF, 0xF3, 0xFF, 0xF8, // ############..###############...
    0xFF, 0xC1, 0xFF, 0xF8, // ##########.....##############...
    0x7E, 0x00, 0xFF, 0xF0, // .######.........############....
    0x30, 0x00, 0xFF, 0xF0, // ..##............############....
    0x00, 0x00, 0xFF, 0xE0, // ................###########.....
    0x00, 0x00, 0xFF, 0xE0, // ................###########.....
    0x00, 0x01, 0xFF, 0xC0, // ...............###########......
    0x00, 0x07, 0xFF, 0x80, // .............############.......
    0x00, 0x0F, 0xFF, 0x00, // ............############........
    0x00, 0x1F, 0xFE, 0x00, // ...........############.........
    0x00, 0x1F, 0xFC, 0x00, // ...........###########..........
    0x00, 0x1F, 0xF0, 0x00, // ...........#########............
    0x00, 0x0F, 0x00, 0x00  // ............####................
};
// --------------------------------------------------------------

void timer2ISR()
{
  x_end_stop.poll();
  y_end_stop.poll();
  ok_button.poll();

  if (ok_button.singleClick())
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);
    ok_button_state = true;
  }

  if (auto result = encoder.process())
  {
    millis() - start < 100 ? encoder_faster = true : encoder_faster = false;
    start = millis();
    result == DIR_CW ? up = true : down = true;
  }
}

void move_home(AccelStepper &stepper, Switch &end_stop, int speed = MOTOR_SPEED, int slow_speed = MOTOR_SLOW_SPEED)
{
  stepper.setSpeed(-speed);
  while (!end_stop.on())
    stepper.runSpeed();
  stepper.setSpeed(slow_speed);
  while (end_stop.on())
    stepper.runSpeed();
  stepper.setSpeed(-slow_speed);
  while (!end_stop.on())
    stepper.runSpeed();
}

void motor_move(AccelStepper &stepper, long steps, int speed = MOTOR_SPEED)
{
  stepper.move(steps);
  steps > 0 ? stepper.setSpeed(speed) : stepper.setSpeed(-speed);
  while (stepper.distanceToGo() != 0)
    stepper.runSpeed();
}

void procedure()
{
  motor_move(unlock, UNLOCK_STEPS);
  motor_move(drive, drive_steps);
  motor_move(lock, MOTOR_REV / 2);
  move_home(unlock, y_end_stop);
  motor_move(knife, MOTOR_REV * 3 * 2L, KNIFE_SPEED);
  motor_move(lock, MOTOR_REV / 2);
}

void draw_init_screen()
{
  u8g.firstPage();
  do
  {
    u8g.setFont(u8g_font_unifont);
    u8g.setFontPosTop();
    u8g.drawStr((u8g.getWidth() - u8g.getStrWidth("Tube Cutter")) / 2, 7, "Tube Cutter");
    u8g.drawBitmapP((u8g.getWidth() - LOGO_WIDTH * 8) / 2, u8g.getHeight() - LOGO_HEIGHT - 5, LOGO_WIDTH, LOGO_HEIGHT, logo_bitmap);
  } while (u8g.nextPage());
}

void select_screen()
{
  EEPROM.get(program_num * sizeof(float), tube_len);
  draw_select_screen();
  while (true)
  {
    if (up)
    {
      menu_current--;
      if (menu_current < 0)
        menu_current = 0;
      up = false;
    }
    else if (down)
    {
      menu_current++;
      if (menu_current > MENU_CNT - 1)
        menu_current = MENU_CNT - 1;
      down = false;
    }

    if (ok_button_state)
    {
      ok_button_state = false;
      switch (menu_current)
      {
      case 0:
        EEPROM.put(program_num * sizeof(float), tube_len);
        while (true)
        {
          if (ok_button_state)
          {
            EEPROM.get(program_num * sizeof(float), tube_len);
            ok_button_state = false;
            break;
          }
          if (up)
          {
            program_num -= 1;
            if (program_num < 1)
              program_num = 1;
            up = false;
          }
          else if (down)
          {
            program_num += 1;
            if (program_num > 100)
              program_num = 100;
            down = false;
          }
          EEPROM.get(program_num * sizeof(float), tube_len);
          draw_select_screen();
        }
        break;
      case 1:
        while (true)
        {
          if (ok_button_state)
          {
            ok_button_state = false;
            break;
          }
          if (up)
          {
            if (!encoder_faster)
              tube_len -= 0.5F;
            else
              tube_len -= 5;
            if (tube_len < 0)
              tube_len = 0;
            up = false;
          }
          else if (down)
          {
            if (!encoder_faster)
              tube_len += 0.5F;
            else
              tube_len += 5;
            down = false;
          }
          draw_select_screen();
        }
        break;
      case 2:
        tube_cnt = 0;
        break;
      case 3:
        while (true)
        {
          if (ok_button_state)
          {
            ok_button_state = false;
            break;
          }
          if (up)
          {
            if (!encoder_faster)
              tube_des -= 10;
            else
              tube_des -= 100;
            if (tube_des < 0)
              tube_des = 0;
            up = false;
          }
          else if (down)
          {
            if (!encoder_faster)
              tube_des += 10;
            else
              tube_des += 100;
            down = false;
          }
          draw_select_screen();
        }
        break;
      case 4:
        menu_elements[menu_current] = "STOP";
        drive_steps = tube_len * MAGIC;
        draw_select_screen();
        while (tube_cnt < tube_des)
        {
          if (ok_button_state)
          {
            ok_button_state = false;
            break;
          }
          procedure();
          tube_cnt++;
          draw_select_screen();
        }
        menu_elements[menu_current] = "RUN";
        break;
      }
    }
    draw_select_screen();
  }
}

void draw_select_screen()
{
  u8g.firstPage();
  do
  {
    u8g.setFont(u8g_font_unifont);
    u8g.setFontPosTop();

    for (int i = 0; i < MENU_CNT; i++)
    {
      if (program_num - 1 < NAMES_CNT)
        u8g.drawStr((u8g.getWidth() - u8g.getStrWidth(program_names[program_num - 1])) / 2, 2, program_names[program_num - 1]);
      u8g.drawLine(0, 18, u8g.getWidth(), 18);
      if (i == menu_current)
      {
        u8g.drawBox((u8g.getWidth() / 2) * (i % 2), (i / 2) * u8g.getFontLineSpacing() + 23 + 1, u8g.getStrWidth(menu_elements[i]), u8g.getFontLineSpacing() - 1);
        u8g.setColorIndex(0);
      }
      u8g.drawStr((u8g.getWidth() / 2) * (i % 2), (i / 2) * u8g.getFontLineSpacing() + 23, menu_elements[i]);
      u8g.setColorIndex(1);
      u8g.setPrintPos((u8g.getWidth() / 2) * (i % 2) + u8g.getStrWidth(menu_elements[i]), (i / 2) * u8g.getFontLineSpacing() + 23);
      switch (i)
      {
      case 0:
        u8g.print(program_num);
        break;
      case 1:
        u8g.print(tube_len, 1);
        break;
      case 2:
        u8g.print(tube_cnt);
        break;
      case 3:
        u8g.print(tube_des);
        break;
      }
    }
  } while (u8g.nextPage());
}

void setup()
{
  FrequencyTimer2::setPeriod(1000);
  FrequencyTimer2::setOnOverflow(timer2ISR);

  encoder.begin();

  pinMode(X_ENABLE_PIN, OUTPUT);
  pinMode(Y_ENABLE_PIN, OUTPUT);
  pinMode(Z_ENABLE_PIN, OUTPUT);
  pinMode(E_ENABLE_PIN, OUTPUT);

  pinMode(FAN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop()
{
  //digitalWrite(BUZZER_PIN, HIGH);
  draw_init_screen();

  digitalWrite(FAN, LOW);

  drive.setMaxSpeed(MOTOR_SPEED);
  lock.setMaxSpeed(MOTOR_SPEED);
  knife.setMaxSpeed(KNIFE_SPEED);
  unlock.setMaxSpeed(MOTOR_SPEED);

  move_home(lock, x_end_stop);
  move_home(unlock, y_end_stop);
  digitalWrite(BUZZER_PIN, LOW);
  select_screen();
}
