import {
  Entity, PrimaryGeneratedColumn, Column,
} from 'typeorm';


@Entity('IoTLog')
export default class IoTLog {
  @PrimaryGeneratedColumn({ type: 'int' })
    id: number;

  @Column('timestamp with time zone', { name: 'time_stamp' })
    timeStamp: Date;

  @Column('jsonb')
    payload: string;

  @Column({ name: 'iot_client_id' })
    iotClientId: string;
}
